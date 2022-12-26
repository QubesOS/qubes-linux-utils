#define _GNU_SOURCE /* For O_NOFOLLOW. */
#define U_HIDE_DEPRECATED_API U_HIDE_DEPRECATED_API
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include <assert.h>
#include <err.h>
#include <inttypes.h>

#include <unicode/uchar.h>
#include <unicode/uscript.h>
#include <unicode/utf.h>

#include "libqubes-rpc-filecopy.h"
#include "ioall.h"
#include "crc32.h"

static char untrusted_namebuf[MAX_PATH_LENGTH];
static unsigned long long bytes_limit = 0;
static unsigned long long files_limit = 0;
static unsigned long long total_bytes = 0;
static unsigned long long total_files = 0;
static int verbose = 0;
/*
 * If positive, wait for disk space before extracting a file,
 * keeping this much extra space (in bytes).
 */
static unsigned long opt_wait_for_space_margin;
static int use_tmpfile = 0;
static int procdir_fd = -1;

void send_status_and_crc(int code, const char *last_filename);

/* copy from asm-generic/fcntl.h */
#ifndef __O_TMPFILE
#define __O_TMPFILE 020000000
#endif
#ifndef O_TMPFILE
/* a horrid kludge trying to make sure that this will fail on old kernels */
#define O_TMPFILE (__O_TMPFILE | O_DIRECTORY)
#define O_TMPFILE_MASK (__O_TMPFILE | O_DIRECTORY | O_CREAT)
#endif

_Noreturn void do_exit(int code, const char *last_filename)
{
    close(0);
    send_status_and_crc(code, last_filename);
    exit(code);
}

void set_size_limit(unsigned long long new_bytes_limit, unsigned long long new_files_limit)
{
    bytes_limit = new_bytes_limit;
    files_limit = new_files_limit;
}

void set_verbose(int value)
{
    verbose = value;
}

void set_wait_for_space(unsigned long value)
{
    opt_wait_for_space_margin = value;
}

void set_procfs_fd(int value)
{
    procdir_fd = value;
    use_tmpfile = 1;
}

int wait_for_space(int fd, unsigned long how_much) {
    int counter = 0;
    struct statvfs fs_space;
    do {
        if (fstatvfs(fd, &fs_space) == -1) {
            perror("fstatvfs");
            return -1;
        }
        // TODO: timeout?
        if (counter > 0)
            usleep(1000000);
        counter++;
    } while (fs_space.f_bsize * fs_space.f_bavail < how_much);
    return 0;
}

static unsigned long crc32_sum = 0;
int read_all_with_crc(int fd, void *buf, int size) {
    int ret;
    ret = read_all(fd, buf, size);
    if (ret)
        crc32_sum = Crc32_ComputeBuf(crc32_sum, buf, size);
    return ret;
}

void send_status_and_crc(int code, const char *last_filename) {
    struct result_header hdr;
    struct result_header_ext hdr_ext;
    int saved_errno;

    saved_errno = errno;
    hdr.error_code = code;
    hdr._pad = 0;
    hdr.crc32 = crc32_sum;
    if (!write_all(1, &hdr, sizeof(hdr)))
        perror("write status");
    if (last_filename) {
        hdr_ext.last_namelen = strlen(last_filename);
        if (!write_all(1, &hdr_ext, sizeof(hdr_ext)))
            perror("write status ext");
        if (!write_all(1, last_filename, hdr_ext.last_namelen))
            perror("write last_filename");
    }
    errno = saved_errno;
}

static long validate_utime_nsec(uint32_t untrusted_nsec)
{
    enum { MAX_NSEC = 999999999L };
    if (untrusted_nsec > MAX_NSEC)
        errx(1, "Invalid nanoseconds value %" PRIu32, untrusted_nsec);
    return (long)untrusted_nsec;
}

static void fix_times_and_perms(const int fd,
        const struct file_header *const untrusted_hdr,
        const char *const last_segment,
        const char *const untrusted_name)
{
    const struct timespec times[2] =
    { 
        {
            .tv_sec = untrusted_hdr->atime,
            .tv_nsec = validate_utime_nsec(untrusted_hdr->atime_nsec)
        },
        {
            .tv_sec = untrusted_hdr->mtime,
            .tv_nsec = validate_utime_nsec(untrusted_hdr->mtime_nsec)
        },
    };
    if (last_segment == NULL) {
        /* Do not change the mode of symbolic links */
        if (!S_ISLNK(untrusted_hdr->mode) &&
            fchmod(fd, untrusted_hdr->mode & 07777))
            do_exit(errno, untrusted_name);
        if (futimens(fd, times))  /* as above */
            do_exit(errno, untrusted_name);
    } else {
        /* Do not change the mode of what a symbolic link points to */
        if (!S_ISLNK(untrusted_hdr->mode) &&
            fchmodat(fd, last_segment, untrusted_hdr->mode & 07777, 0))
            do_exit(errno, untrusted_name);
        if (utimensat(fd, last_segment, times, AT_SYMLINK_NOFOLLOW))  /* as above */
            do_exit(errno, untrusted_name);
    }
}

/* validate single UTF-8 character
 * return bytes count of this character, or 0 if the character is invalid */
static int validate_utf8_char(const unsigned char *untrusted_c) {
    int tails_count = 0;
    int total_size = 0;
    uint32_t code_point;
    /* it is safe to access byte pointed by the parameter,
     * but every next byte can access only if previous byte was not NUL.
     */

    /* According to http://www.ietf.org/rfc/rfc3629.txt:
     *   UTF8-char   = UTF8-1 / UTF8-2 / UTF8-3 / UTF8-4
     *   UTF8-1      = %x00-7F
     *   UTF8-2      = %xC2-DF UTF8-tail
     *   UTF8-3      = %xE0 %xA0-BF UTF8-tail / %xE1-EC 2( UTF8-tail ) /
     *                 %xED %x80-9F UTF8-tail / %xEE-EF 2( UTF8-tail )
     *   UTF8-4      = %xF0 %x90-BF 2( UTF8-tail ) / %xF1-F3 3( UTF8-tail ) /
     *                 %xF4 %x80-8F 2( UTF8-tail )
     *   UTF8-tail   = %x80-BF
     *
     * This code uses a slightly different grammar:
     *
     *   UTF8-char   = UTF8-1 / UTF8-2 / UTF8-3 / UTF8-4
     *   UTF8-1      = %x20-7F
     *   UTF8-2      = %xC2-DF UTF8-tail
     *   UTF8-3      = %xE0 %xA0-BF UTF8-tail / %xE1-EF 2( UTF8-tail )
     *   UTF8-4      = %xF0 %x90-BF 2( UTF8-tail ) / %xF1-F4 3( UTF8-tail )
     *   UTF8-tail   = %x80-BF
     *
     * The differences are:
     *
     * - ASCII control characters are rejected, allowing a fast path for other
     *   ASCII characters.
     * - Surrogates and some values above 0x10FFFF are accepted here, but are
     *   rejected as forbidden code points later.
     */
    switch (*untrusted_c) {
        case 0xC2 ... 0xDF:
            total_size = 2;
            tails_count = 1;
            code_point = *untrusted_c & 0x1F;
            break;
        case 0xE0:
            untrusted_c++;
            total_size = 3;
            if (*untrusted_c >= 0xA0 && *untrusted_c <= 0xBF)
                tails_count = 1;
            else
                return 0;
            code_point = *untrusted_c & 0x3F;
            break;
        case 0xE1 ... 0xEF:
            total_size = 3;
            tails_count = 2;
            code_point = *untrusted_c & 0xF;
            break;
        case 0xF0:
            untrusted_c++;
            total_size = 4;
            if (*untrusted_c >= 0x90 && *untrusted_c <= 0xBF)
                tails_count = 2;
            else
                return 0;
            code_point = *untrusted_c & 0x3F;
            break;
        case 0xF1 ... 0xF4:
            total_size = 4;
            tails_count = 3;
            code_point = *untrusted_c & 0x7;
            break;
        default:
            return 0; // control ASCII or invalid UTF-8
    }

    while (tails_count-- > 0) {
        untrusted_c++;
        if (!(*untrusted_c >= 0x80 && *untrusted_c <= 0xBF))
            return 0;
        code_point = code_point << 6 | (*untrusted_c & 0x3F);
    }

    /*
     * Validate that this is a Unicode codepoint that can be assigned a
     * character.  This catches surrogates, code points beyond 0x10FFFF, and
     * various noncharacters.
     */
    if (!(U_IS_UNICODE_CHAR(code_point)))
        return 0;

    switch (code_point) {
#include "unpack-table.c"
        return 0; // Invalid UTF-8 or forbidden codepoint
    default:
        break;
    }

    uint32_t s = u_charDirection(code_point);
    switch (s) {
    case U_WHITE_SPACE_NEUTRAL:
    case U_OTHER_NEUTRAL:
    case U_EUROPEAN_NUMBER_TERMINATOR:
    case U_EUROPEAN_NUMBER_SEPARATOR:
    case U_COMMON_NUMBER_SEPARATOR:
    case U_EUROPEAN_NUMBER:
    case U_LEFT_TO_RIGHT:
        break;
    default:
        /* Not safe */
        return 0;
    }
    UErrorCode errcode = 0;
    int script = uscript_getScript(code_point, &errcode);
    if (errcode) {
        fprintf(stderr, "BUG: uscript_getScript failed on codepoint 0x%" PRIX32 " with with code %d\n", code_point, errcode);
        abort();
    }
    switch (script) {
    case USCRIPT_INHERITED:
    case USCRIPT_CYRILLIC:
    case USCRIPT_GREEK:
    case USCRIPT_LATIN:
    case USCRIPT_BRAILLE:
    case USCRIPT_SIMPLIFIED_HAN:
    case USCRIPT_TRADITIONAL_HAN:
    case USCRIPT_HAN:
    case USCRIPT_HAN_WITH_BOPOMOFO:
    case USCRIPT_JAMO:
    case USCRIPT_HANGUL:
    case USCRIPT_BOPOMOFO:
    case USCRIPT_KATAKANA_OR_HIRAGANA:
    case USCRIPT_HIRAGANA:
    case USCRIPT_KATAKANA:
    case USCRIPT_JAPANESE:
    case USCRIPT_KOREAN:
    case USCRIPT_COMMON:
        return total_size;
    case USCRIPT_DESERET:
    case USCRIPT_COPTIC:
    case USCRIPT_LINEAR_B:
    case USCRIPT_ETHIOPIC:
    case USCRIPT_GOTHIC:
    case USCRIPT_OGHAM:
    case USCRIPT_OLD_ITALIC:
    case USCRIPT_UGARITIC:
    case USCRIPT_GLAGOLITIC:
    case USCRIPT_KHAROSHTHI:
    case USCRIPT_OLD_PERSIAN:
    case USCRIPT_HIERATIC_EGYPTIAN:
    case USCRIPT_EGYPTIAN_HIEROGLYPHS:
    case USCRIPT_LINEAR_A:
    case USCRIPT_DEMOTIC_EGYPTIAN:
    case USCRIPT_BRAHMI:
    case USCRIPT_KHUTSURI:
    case USCRIPT_OLD_HUNGARIAN:
    case USCRIPT_HARAPPAN_INDUS:
    case USCRIPT_MAYAN_HIEROGLYPHS:
    case USCRIPT_MEROITIC_HIEROGLYPHS:
    case USCRIPT_OLD_PERMIC:
    case USCRIPT_PHOENICIAN:
    case USCRIPT_ORKHON:
    case USCRIPT_RONGORONGO:
    case USCRIPT_CUNEIFORM:
    case USCRIPT_CARIAN:
    case USCRIPT_LYCIAN:
    case USCRIPT_LYDIAN:
    case USCRIPT_REJANG:
    case USCRIPT_IMPERIAL_ARAMAIC:
    case USCRIPT_AVESTAN:
    case USCRIPT_KAITHI:
    case USCRIPT_INSCRIPTIONAL_PAHLAVI:
    case USCRIPT_PSALTER_PAHLAVI:
    case USCRIPT_BOOK_PAHLAVI:
    case USCRIPT_SAMARITAN:
    case USCRIPT_INSCRIPTIONAL_PARTHIAN:
    case USCRIPT_ELBASAN:
    case USCRIPT_CAUCASIAN_ALBANIAN:
    case USCRIPT_PALMYRENE:
    case USCRIPT_NABATAEAN:
    case USCRIPT_HATRAN:
    case USCRIPT_MEROITIC_CURSIVE:
    case USCRIPT_OLD_SOUTH_ARABIAN:
    case USCRIPT_OLD_NORTH_ARABIAN:
    case USCRIPT_OLD_CHURCH_SLAVONIC_CYRILLIC:
    case USCRIPT_OLD_SOGDIAN:
    case USCRIPT_SOGDIAN:
#ifdef USCRIPT_CHORASMIAN
    case USCRIPT_CHORASMIAN:
#endif
    case USCRIPT_ELYMAIC:
    case USCRIPT_MAHAJANI:
    case USCRIPT_JURCHEN:
    case USCRIPT_TANGUT:
    case USCRIPT_WOLEAI:
    case USCRIPT_ANATOLIAN_HIEROGLYPHS:
    case USCRIPT_KHOJKI:
    case USCRIPT_MULTANI:
    case USCRIPT_MODI:
    case USCRIPT_AHOM:
    case USCRIPT_DOGRA:
    case USCRIPT_BHAIKSUKI:
    case USCRIPT_MARCHEN:
    case USCRIPT_ZANABAZAR_SQUARE:
#ifdef USCRIPT_DIVES_AKURU
    case USCRIPT_DIVES_AKURU:
#endif
    case USCRIPT_MAKASAR:
    case USCRIPT_NANDINAGARI:
#ifdef USCRIPT_KHITAN_SMALL_SCRIPT
    case USCRIPT_KHITAN_SMALL_SCRIPT:
#endif
        return 0; // dead languages or scripts
    case USCRIPT_MENDE:
    case USCRIPT_MANDAIC:
    case USCRIPT_ARABIC:
    case USCRIPT_HEBREW:
    case USCRIPT_NKO:
    case USCRIPT_HANIFI_ROHINGYA:
    case USCRIPT_NUSHU:
    case USCRIPT_ADLAM:
        return 0; // right-to-left
    case USCRIPT_DEVANAGARI:
    case USCRIPT_SYRIAC:
    case USCRIPT_BENGALI:
    case USCRIPT_BALINESE:
    case USCRIPT_ESTRANGELO_SYRIAC:
    case USCRIPT_WESTERN_SYRIAC:
    case USCRIPT_EASTERN_SYRIAC:
    case USCRIPT_GUJARATI:
    case USCRIPT_GURMUKHI:
    case USCRIPT_KANNADA:
    case USCRIPT_KHMER:
    case USCRIPT_MALAYALAM:
    case USCRIPT_MONGOLIAN:
    case USCRIPT_MYANMAR:
    case USCRIPT_THAI:
    case USCRIPT_SINHALA:
    case USCRIPT_TAMIL:
    case USCRIPT_TELUGU:
    case USCRIPT_THAANA:
    case USCRIPT_TIBETAN:
    case USCRIPT_ORIYA:
    case USCRIPT_PHAGS_PA:
    case USCRIPT_LIMBU:
    case USCRIPT_LAO:
    case USCRIPT_TAGALOG:
    case USCRIPT_BUHID:
    case USCRIPT_TAI_LE:
    case USCRIPT_BUGINESE:
    case USCRIPT_BATAK:
    case USCRIPT_CHAM:
    case USCRIPT_JAVANESE:
    case USCRIPT_LEPCHA:
    case USCRIPT_MIAO:
    case USCRIPT_LANNA:
    case USCRIPT_SAURASHTRA:
    case USCRIPT_CHAKMA:
    case USCRIPT_TAI_VIET:
    case USCRIPT_KHUDAWADI:
    case USCRIPT_TAKRI:
    case USCRIPT_NEWA:
    case USCRIPT_SOYOMBO:
    case USCRIPT_SIGN_WRITING:
        return 0; // require complex rendering
    default:
        return 0; // not sure
    }
}

static size_t validate_path(const char *const untrusted_name, size_t allowed_leading_dotdot)
{
    if (untrusted_name[0] == 0)
        do_exit(ENOENT, untrusted_name); // same error as Linux
    size_t non_dotdot_components = 0, i = 0;
    do {
        if (i == 0 || untrusted_name[i - 1] == '/') {
            switch (untrusted_name[i]) {
            case '/': // repeated or initial slash
            case '\0': // trailing slash
                do_exit(EILSEQ, untrusted_name);
            case '.':
                if (untrusted_name[i + 1] == '\0' || untrusted_name[i + 1] == '/')
                    do_exit(EILSEQ, untrusted_name); // path component is "."
                if ((untrusted_name[i + 1] == '.') &&
                    (untrusted_name[i + 2] == '\0' || untrusted_name[i + 2] == '/')) {
                    if (allowed_leading_dotdot > 0) {
                        allowed_leading_dotdot--;
                        i += 2; // advance past ".."
                        continue;
                    }
                    do_exit(EILSEQ, untrusted_name); // too many ".." components
                }
                __attribute__((fallthrough));
            default:
                allowed_leading_dotdot = 0; // do not allow further ".." components
                non_dotdot_components++;
                break;
            }
        }
        if (untrusted_name[i] >= 0x20 && untrusted_name[i] <= 0x7E) {
            i++;
        } else {
            int utf8_ret = validate_utf8_char((const unsigned char *)(untrusted_name + i));
            if (utf8_ret > 0) {
                i += utf8_ret;
            } else {
                do_exit(EILSEQ, untrusted_name); // not valid UTF-8
            }
        }
    } while (untrusted_name[i]);
    return non_dotdot_components;
}

// Open the second-to-last component of a path, enforcing O_NOFOLLOW for every
// path component.  *last_segment will be set to the last segment of the path,
// and points into the original path.  The original path is modified in-place,
// so one should probably pass a copy.  The return value is either dirfd (if the
// path has no / in it) or a newly opened file descriptor that must be closed by
// the caller.  dirfd can be AT_FDCWD to indicate the current_directory.
static int opendir_safe(int dirfd, char *path, const char **last_segment)
{
    assert(path && *path); // empty paths rejected earlier
    char *this_segment = path, *next_segment = NULL;
    *last_segment = NULL;
    int cur_fd = dirfd;
    for (;;this_segment = next_segment) {
        assert(this_segment);
        char *next = strchr(this_segment, '/');
        if (next == NULL) {
            *last_segment = this_segment;
            return cur_fd;
        }
        *next = '\0';
        next_segment = next + 1;
        if ((next - this_segment <= 2) &&
            (memcmp(this_segment, "..", (size_t)(next - this_segment)) == 0)) {
            fprintf(stderr, "BUG: path component '%s' not rejected earlier!\n", this_segment);
            abort();
        }
        int new_fd = openat(cur_fd, this_segment, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_NOCTTY | O_CLOEXEC);
        if (new_fd == -1)
            do_exit(errno, this_segment);
        if (cur_fd != dirfd)
            close(cur_fd);
        cur_fd = new_fd;
    }
}

void process_one_file_reg(struct file_header *untrusted_hdr,
        const char *untrusted_name)
{
    int ret;
    int fdout = -1, safe_dirfd;
    const char *last_segment;
    char *path_dup;

    validate_path(untrusted_name, 0);
    if ((path_dup = strdup(untrusted_name)) == NULL)
        do_exit(ENOMEM, untrusted_name);
    safe_dirfd = opendir_safe(AT_FDCWD, path_dup, &last_segment);

    /* make the file inaccessible until fully written */
    if (use_tmpfile) {
        fdout = openat(safe_dirfd, ".", O_WRONLY | O_TMPFILE | O_CLOEXEC | O_NOCTTY, 0700);
        if (fdout < 0) {
            if (errno==ENOENT || /* most likely, kernel too old for O_TMPFILE */
                    errno==EOPNOTSUPP) /* filesystem has no support for O_TMPFILE */
                use_tmpfile = 0;
            else
                do_exit(errno, untrusted_name);
        }
    }

    if (fdout < 0)
        fdout = openat(safe_dirfd, last_segment, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC | O_NOCTTY, 0000);
    if (fdout < 0)
        do_exit(errno, untrusted_name);
    if (safe_dirfd != AT_FDCWD)
        close(safe_dirfd);

    /* sizes are signed elsewhere */
    if (untrusted_hdr->filelen > LLONG_MAX || (bytes_limit && untrusted_hdr->filelen > bytes_limit))
        do_exit(EDQUOT, untrusted_name);
    if (bytes_limit && total_bytes > bytes_limit - untrusted_hdr->filelen)
        do_exit(EDQUOT, untrusted_name);
    if (opt_wait_for_space_margin) {
        wait_for_space(fdout,
            untrusted_hdr->filelen + opt_wait_for_space_margin);
    }
    total_bytes += untrusted_hdr->filelen;
    ret = copy_file(fdout, 0, untrusted_hdr->filelen, &crc32_sum);
    if (ret != COPY_FILE_OK) {
        if (ret == COPY_FILE_READ_EOF
                || ret == COPY_FILE_READ_ERROR)
            do_exit(LEGAL_EOF, untrusted_name); // hopefully remote will produce error message
        else
            do_exit(errno, untrusted_name);
    }
    if (use_tmpfile) {
        char fd_str[11];
        if ((unsigned)snprintf(fd_str, sizeof(fd_str), "%d", fdout) >= sizeof(fd_str))
            abort();
        if (linkat(procdir_fd, fd_str, AT_FDCWD, untrusted_name, AT_SYMLINK_FOLLOW) < 0)
            do_exit(errno, untrusted_name);
    }
    fix_times_and_perms(fdout, untrusted_hdr, NULL, untrusted_name);
    close(fdout);
    free(path_dup);
}


void process_one_file_dir(struct file_header *untrusted_hdr,
        const char *untrusted_name)
{
    int safe_dirfd;
    const char *last_segment;
    char *path_dup;
    validate_path(untrusted_name, 0);
    if ((path_dup = strdup(untrusted_name)) == NULL)
        do_exit(ENOMEM, untrusted_name);
    safe_dirfd = opendir_safe(AT_FDCWD, path_dup, &last_segment);

    // fix perms only when the directory is sent for the second time
    // it allows to transfer r.x directory contents, as we create it rwx initially
    struct stat buf;
    if (!mkdirat(safe_dirfd, last_segment, 0700)) {
        close(safe_dirfd);
        return;
    }
    if (errno != EEXIST)
        do_exit(errno, untrusted_name);
    if (stat(untrusted_name,&buf) < 0)
        do_exit(errno, untrusted_name);
    total_bytes += buf.st_size;
    /* size accumulated after the fact, so don't check limit here */
    fix_times_and_perms(safe_dirfd, untrusted_hdr, last_segment, untrusted_name);
    if (safe_dirfd != AT_FDCWD)
        close(safe_dirfd);
    free(path_dup);
}

void process_one_file_link(struct file_header *untrusted_hdr,
        const char *untrusted_name)
{
    char untrusted_content[MAX_PATH_LENGTH];
    const char *last_segment;
    char *path_dup;
    unsigned int filelen;
    size_t path_components = validate_path(untrusted_name, 0);
    int safe_dirfd;
    if (untrusted_hdr->filelen > MAX_PATH_LENGTH - 1)
        do_exit(ENAMETOOLONG, untrusted_name);

    filelen = untrusted_hdr->filelen; /* sanitized above */
    total_bytes += filelen;
    if (bytes_limit && total_bytes > bytes_limit)
        do_exit(EDQUOT, untrusted_name);
    if (!read_all_with_crc(0, untrusted_content, filelen))
        do_exit(LEGAL_EOF, untrusted_name); // hopefully remote has produced error message
    untrusted_content[filelen] = 0;
    if (path_components < 1)
        abort(); // validate_path() should not have returned
    validate_path(untrusted_content, path_components - 1);

    if ((path_dup = strdup(untrusted_name)) == NULL)
        do_exit(ENOMEM, untrusted_name);
    safe_dirfd = opendir_safe(AT_FDCWD, path_dup, &last_segment);

    if (symlinkat(untrusted_content, safe_dirfd, last_segment))
        do_exit(errno, untrusted_name);

    if (safe_dirfd != AT_FDCWD)
        close(safe_dirfd);
    free(path_dup);
}

void process_one_file(struct file_header *untrusted_hdr, int flags)
{
    unsigned int namelen;
    if (untrusted_hdr->namelen > MAX_PATH_LENGTH - 1)
        do_exit(ENAMETOOLONG, NULL); /* filename too long so not received at all */
    namelen = untrusted_hdr->namelen; /* sanitized above */
    if (!read_all_with_crc(0, untrusted_namebuf, namelen))
        do_exit(LEGAL_EOF, NULL); // hopefully remote has produced error message
    untrusted_namebuf[namelen] = 0;
    if (S_ISREG(untrusted_hdr->mode))
        process_one_file_reg(untrusted_hdr, untrusted_namebuf);
    else if (S_ISLNK(untrusted_hdr->mode) && (flags & COPY_ALLOW_SYMLINKS))
        process_one_file_link(untrusted_hdr, untrusted_namebuf);
    else if (S_ISDIR(untrusted_hdr->mode) && (flags & COPY_ALLOW_DIRECTORIES))
        process_one_file_dir(untrusted_hdr, untrusted_namebuf);
    else
        do_exit(EINVAL, untrusted_namebuf);
    if (verbose && !S_ISDIR(untrusted_hdr->mode))
        fprintf(stderr, "%s\n", untrusted_namebuf);
}

int do_unpack(void) {
    return do_unpack_ext(COPY_ALLOW_DIRECTORIES | COPY_ALLOW_SYMLINKS);
}

int do_unpack_ext(int flags)
{
    struct file_header untrusted_hdr;
    int cwd_fd;
    int saved_errno;

    total_bytes = total_files = 0;
    /* initialize checksum */
    crc32_sum = 0;
    while (read_all_with_crc(0, &untrusted_hdr, sizeof untrusted_hdr)) {
        /* check for end of transfer marker */
        if (untrusted_hdr.namelen == 0) {
            errno = 0;
            break;
        }
        total_files++;
        if (files_limit && total_files > files_limit)
            do_exit(EDQUOT, untrusted_namebuf);
        process_one_file(&untrusted_hdr, flags);
    }

    saved_errno = errno;
    cwd_fd = open(".", O_RDONLY);
    if (cwd_fd >= 0 && syncfs(cwd_fd) == 0 && close(cwd_fd) == 0)
        errno = saved_errno;

    send_status_and_crc(errno, untrusted_namebuf);
    return errno;
}
