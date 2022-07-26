#!/usr/bin/python3 --
def main():
    def print_interval(interval, last_cat):
        if last_cat == 'Cn':
            return
        if interval[0] != interval[1]:
            print(f'    case 0x{interval[0]:X} ... 0x{interval[1]:X}: // category {last_cat}')
        else:
            print(f'    case 0x{interval[0]:X}: // category {last_cat}')
    import sys
    interval = [0, 0]
    from unicodedata import category, name
    cat = last_cat = 'Cc'
    for i in range(0, 0x110000):
        last_cat = cat
        cat = category(chr(i))
        if cat == last_cat:
            interval[1] = i
        else:
            # Allow the interval consisting only of 0x20, to allow spaces in
            # file names
            if last_cat[0] in ('C', 'Z') and interval != [0x20, 0x20]:
                print_interval(interval, last_cat)
            interval = [i, i]
    print_interval(interval, last_cat)
if __name__ == '__main__':
    main()
