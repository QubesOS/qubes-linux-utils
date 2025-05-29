#!/usr/bin/ash

run_earlyhook() {

	msg "Starting Qubes copy on write setup script"

	/usr/lib/qubes/scrub_pages.sh
	/usr/lib/qubes/qubes_cow_setup.sh

}
