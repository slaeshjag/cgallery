#ifndef ALBUMSCN_H__
#define	ALBUMSCN_H__

#include <time.h>
#include <stdbool.h>

struct VariantDescription {
	char				*variant;
	char				*title;
	char				*text_body;
	int				sort_value;
	time_t				last_update; // Last update is the modify-time of the metadata
	bool				needs_update;
};


struct AlbumLevelEntry {
	char				*dirname;
	struct VariantDescription	*variant;
	int				variants;
	bool				needs_update;
	struct AlbumLevel		*child;
};


struct AlbumPictureEntry {
	char				*fname;
	struct VariantDescription	*variant;
	int				variants;
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
	char				**variant;
	char				variants;
};


#endif
