#include <dirent.h>
#include <time.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>


struct VariantDescription {
	char				*variant;
	char				*title;
	char				*text_body;
	int				sort_value;
	time_t				last_update; // Last update is the modify-time of the metadata
};


struct AlbumLevelEntry {
	char				*dirname;
	struct VariantDescription	*variant;
	int				variants;
	struct AlbumLevel		*child;
};


struct AlbumPictureEntry {
	char				*fname;
	struct VariantDescription	*variant;
	int				variants;

};


struct AlbumLevel {
	struct AlbumLevelEntry		*subalbum;
	int				subalbums;
	struct AlbumPictureEntry	*picture;
	int				pictures;
};


int album_add_subalbum(struct AlbumLevel *al) {
	int id = al->subalbums++;
	al->subalbum = realloc(al->subalbum, sizeof(*al->subalbum) * al->subalbums);
	al->subalbum[id].variant = NULL, al->subalbum[id].variants = 0;
	return id;
}


static char *_get_extension(char *fname) {
	char *last, *cur;
	
	last = NULL;
	for (cur = strchr(fname, '.'); cur; last = cur, cur = strchr(cur + 1, '.'));
	return last;
}


void album_locate_variants(const char *path, struct AlbumLevelEntry *ale) {
	DIR *album;
	struct dirent dent, *result;
	struct stat st;
	char *variant, buff[PATH_MAX], *tmp;
	int v, i;
	FILE *fp;
	off_t pos, body_len;

	album = opendir(path);
	for (readdir_r(album, &dent, &result); result; readdir_r(album, &dent, &result)) {
		if (strstr(dent.d_name, ale->dirname) != dent.d_name)
			continue;
		if (!(variant = _get_extension(dent.d_name)))
			continue;
		snprintf(buff, PATH_MAX, "%s/%s", path, dent.d_name);
		if (stat(buff, &st) < 0)
			continue;
		variant++;
		v = ale->variants++;
		ale->variant = realloc(ale->variant, sizeof(*ale->variant) * ale->variants);
		ale->variant[v].last_update = st.st_mtim.tv_sec;
		if (!(fp = fopen(buff, "r"))) {
			ale->variants--;
			continue;
		}

		if (fgets(buff, PATH_MAX, fp) < 0)
			snprintf(buff, PATH_MAX, ale->dirname);
		if (*buff == '|') {	/* Picture/album is hidden in this variant */
			ale->variants--; 
			continue;
		}

		ale->variant[v].variant = strdup(variant);
		if (fgets(buff, PATH_MAX, fp) < 0)
			snprintf(buff, PATH_MAX, ale->dirname);
		if ((tmp = strchr(buff, '\n')))
			*tmp = 0;
		ale->variant[v].title = strdup(buff);
		ale->variant[v].sort_value = fscanf(fp, "%i\n", &i) < 1 ? -1 : i;
		pos = ftell(fp);
		fseek(fp, 0, SEEK_END);
		body_len = ftell(fp) - pos;
		fseek(fp, pos, SEEK_SET);
		ale->variant[v].text_body = malloc(body_len + 1);
		fread(ale->variant[v].text_body, body_len, 1, fp);
		ale->variant[v].text_body[body_len] = 0;
	}

	closedir(album);
}



static bool _is_supported_picture_type(char *fname) {
	char *ext;
	const char* const extensions[] = { ".jpg", ".jpeg", ".png", ".gif", NULL };
	int i;

	if (!(ext = _get_extension(fname)))
		return false;
	for (i = 0; extensions[i]; i++)
		if (!strcasecmp(fname, extensions[i]))
			return true;
	return false;
}


struct AlbumLevel *album_scan(const char *base_path) {
	DIR *album;
	struct dirent dent, *result;
	struct stat st;
	char buff[PATH_MAX];
	int a;
	struct AlbumLevel *al;

	if (!(album = opendir(base_path)))
		return NULL;
	
	al = malloc(sizeof(*al));
	al->subalbum = NULL, al->subalbums = 0;

	for (readdir_r(album, &dent, &result); result; readdir_r(album, &dent, &result)) {
		if (_is_supported_picture_type(dent.d_name)) {
			a = al->pictures++;
			al->picture = realloc(al->picture, sizeof(*al->picture) * al->pictures);
			al->picture[a].fname = strdup(dent.d_name);
			al->picture[a].variant = NULL, al->picture[a].variants = 0;
			album_locate_variants(base_path, (struct AlbumLevelEntry *) &al->picture[a]); /* Casting like this is a bit evil, but if we're careful, nobody gets hurt */
		} else {
			if (*dent.d_name == '.')
				continue;
			if (strchr(dent.d_name, '.'))
				continue;
			snprintf(buff, PATH_MAX, "%s/%s", base_path, dent.d_name);
			if (stat(buff, &st) < 0)
				continue;
			if (!S_ISDIR(st.st_mode))
				continue;
			a = album_add_subalbum(al);
			al->subalbum[a].dirname = strdup(dent.d_name);
			album_locate_variants(base_path, &al->subalbum[a]);
			al->subalbum[a].child = album_scan(buff);
		}
	}

	closedir(album);
	return al;
}
