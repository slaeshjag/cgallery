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

#include "albumscn.h"


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
		ale->variant[v].needs_update = false;
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


static struct AlbumLevel *_scan(const char *base_path) {
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
			snprintf(buff, PATH_MAX, "%s/%s", base_path, dent.d_name);
			stat(buff, &st);
			al->picture[a].last_modified = st.st_mtim.tv_sec;
			al->picture[a].variant = NULL, al->picture[a].variants = 0;
			album_locate_variants(base_path, (struct AlbumLevelEntry *) &al->picture[a]); /* Casting like this is a bit evil, but if we're careful, nobody gets hurt */
			al->picture[a].needs_update = false;
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
			al->subalbum[a].child = _scan(buff);
			al->picture[a].needs_update = false;
		}
	}

	closedir(album);

	if (!al->subalbums && !al->pictures)
		return free(al), NULL;
	return al;
}


struct Album *album_crawl(const char *path) {
	struct Album *a;


	a = malloc(sizeof(*a));
	a->root = _scan(path);
	a->variant = NULL, a->variants = 0;
	return a;

}


static void _add_variant(struct Album *a, const char *variant) {
	int i;

	for (i = 0; i < a->variants; i++)
		if (!strcmp(a->variant[i], variant))
			return;
	a->variants++;
	a->variant = realloc(a->variant, sizeof(*a->variant) * a->variants);
	a->variant[i] = strdup(variant);
}


void album_identify_variants(struct Album *a, struct AlbumLevel *cur) {
	int i, j;

	if (!cur)
		return;
	for (i = 0; i < cur->subalbums; i++) {
		album_identify_variants(a, cur->subalbum[i].child);
		for (j = 0; j < cur->subalbum[i].variants; j++)
			_add_variant(a, cur->subalbum[i].variant[j].variant);
	}
	
	for (i = 0; i < cur->pictures; i++)
		for (j = 0; j < cur->picture[i].variants; j++)
			_add_variant(a, cur->picture[i].variant[j].variant);
	return;
}


void _mark_outdated_recursive(struct AlbumLevel *al, bool thumbs) {
	int i, j;

	if (!al)
		return;

	for (i = 0; i < al->subalbums; i++) {
		_mark_outdated_recursive(al->subalbum[i].child, thumbs);
		al->subalbum[i].needs_update = true;
	}

	for (i = 0; i < al->pictures; i++) {
		if (thumbs)
			al->picture[i].needs_update = true;
		for (j = 0; j < al->picture[i].variants; j++)
			al->picture[i].variant[j].needs_update = true;
	}
}


void _locate_outdated(struct AlbumLevel *al, const char *path, const char *variant) {
	char buff[PATH_MAX];
	struct stat st;
	int i, j;

	for (i = 0; i < al->subalbums; i++) {
		snprintf(buff, PATH_MAX, "%s/%s", path, al->subalbum[i].dirname);
		if (stat(buff, &st) < 0)
			return _mark_outdated_recursive(al, false), _mark_outdated_recursive(al->subalbum[i].child, true);
		_locate_outdated(al, path, variant);
	}

	for (i = 0; i < al->pictures; i++) {
		snprintf(buff, PATH_MAX, "%s/%s.cgal", path, al->picture[i].fname);
		for (j = 0; j < al->picture[i].variants; j++)
			if (!strcmp(al->picture[i].variant[j].variant, variant))
				break;
		if (j == al->picture[i].variants) {
			if (stat(buff, &st) >= 0)
				al->picture[i].variant[j].needs_update = true;
			goto thumbnail;
		}
		if (stat(buff, &st) < 0) {
			al->picture[i].variant[j].needs_update = true;
			goto thumbnail;
		}
		if (al->picture[i].variant[j].last_update > st.st_mtim.tv_sec)
			al->picture[i].variant[j].needs_update = true;
	thumbnail:
		snprintf(buff, PATH_MAX, "%s/%s_thumb", path, al->picture[i].fname);
		if (stat(buff, &st) < 0)
			al->picture[i].needs_update = true;
		else if (st.st_mtim.tv_sec < al->picture[i].last_modified)
			al->picture[i].needs_update = true;
		goto small_photo;
	
	small_photo:
		snprintf(buff, PATH_MAX, "%s/%s_small", path, al->picture[i].fname);
		if (stat(buff, &st) < 0)
			al->picture[i].needs_update = true;
		else if (st.st_mtim.tv_sec < al->picture[i].last_modified)
			al->picture[i].needs_update = true;
	}
}


void album_locate_outdated(struct Album *a, const char *path_target) {
	char buff[PATH_MAX];
	int i;

	for (i = 0; i < a->variants; i++) {
		snprintf(buff, PATH_MAX, "%s/%s", path_target, a->variant[i]);
		_locate_outdated(a->root, buff, a->variant[i]);
	}
}
