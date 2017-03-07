#ifndef ALBUMSCN_H__
#define	ALBUMSCN_H__

#include <time.h>
#include <stdbool.h>

struct VariantDescription {
	char				*title;
	char				*text_body;
	int				sort_value;
	time_t				last_update; // Last update is the modify-time of the metadata
	bool				needs_update;
	bool				hidden;
};


struct AlbumLevelEntry {
	char				*dirname;
	struct VariantDescription	variant;
	bool				needs_update;
	struct AlbumLevel		*child;
};


struct AlbumPictureEntry {
	char				*fname;
	struct VariantDescription	variant;
	bool				needs_update;
	time_t				last_modified;
};


struct AlbumLevel {
	struct AlbumLevelEntry		*subalbum;
	int				subalbums;
	struct AlbumPictureEntry	*picture;
	int				pictures;
};


struct Album {
	struct AlbumLevel		*root;
};


struct Album *album_crawl(const char *path, const char *fext);
void album_locate_outdated(struct Album *a, const char *path_target, const char *fext);
void album_locate_obsolete(struct AlbumLevel *al, const char *path_target, bool has_data);


#endif
