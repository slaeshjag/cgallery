#include "albumscn.h"


int main(int argc, char **argv) {
	struct Album *a;

	a = album_crawl(argv[1], argv[3]);
	album_locate_outdated(a, argv[2], argv[3]);
	album_locate_obsolete(a->root, argv[2], true);
	return 0;
}
