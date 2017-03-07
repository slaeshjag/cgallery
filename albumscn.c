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
	memset(&al->subalbum[id].variant, 0, sizeof(al->subalbum[id].variant));
	return id;
}


static char *_get_extension(char *fname) {
	char *last, *cur;
	
	last = NULL;
	for (cur = strchr(fname, '.'); cur; last = cur, cur = strchr(cur + 1, '.'));
	return last;
}


static void _load_description(const char *path, struct AlbumLevelEntry *ale, const char *fext) {
	char buff[PATH_MAX], *tmp;
	int i;
	FILE *fp;
	off_t pos, body_len;

	snprintf(buff, PATH_MAX, "%s/%s%s", path, ale->dirname, fext);
	ale->variant.hidden = ale->needs_update = false;
	ale->variant.text_body = NULL, ale->variant.title = NULL;
	if ((fp = fopen(buff, "r"))) {
		if (strlen(fgets(buff, PATH_MAX, fp)) < 2)
			snprintf(buff, PATH_MAX, ale->dirname);
		if (*buff == '|') {	/* Picture/album is hidden in this variant */
			ale->variant.hidden = true;
			return;
		}

		if ((tmp = strchr(buff, '\n')))
			*tmp = 0;
		ale->variant.title = strdup(buff);
		ale->variant.sort_value = fscanf(fp, "%i\n", &i) < 1 ? -1 : i;
		pos = ftell(fp);
		fseek(fp, 0, SEEK_END);
		body_len = ftell(fp) - pos;
		fseek(fp, pos, SEEK_SET);
		ale->variant.text_body = malloc(body_len + 1);
		fread(ale->variant.text_body, body_len, 1, fp);
		ale->variant.text_body[body_len] = 0;
		ale->variant.needs_update = false;
	} else
		ale->variant.title = strdup(ale->dirname);
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


static struct AlbumLevel *_scan(const char *base_path, const char *fext) {
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
			if (stat(buff, &st) < 0)
				continue;
			if (S_ISDIR(st.st_mode))
				continue; /* Don't be a fool, file extension isn't everything */
			a = al->pictures++;
			al->picture = realloc(al->picture, sizeof(*al->picture) * al->pictures);
			al->picture[a].fname = strdup(dent.d_name);
			snprintf(buff, PATH_MAX, "%s/%s", base_path, dent.d_name);
			al->picture[a].last_modified = st.st_mtim.tv_sec;
			_load_description(base_path, (struct AlbumLevelEntry *) &al->picture[a], fext); /* Casting like this is a bit evil, but if we're careful, nobody gets hurt */
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
			_load_description(base_path, &al->subalbum[a], fext);
			al->subalbum[a].child = _scan(buff, fext);
			al->picture[a].needs_update = false;
		}
	}

	closedir(album);

	if (!al->subalbums && !al->pictures)
		return free(al), NULL;
	return al;
}


struct Album *album_crawl(const char *path, const char *fext) {
	struct Album *a;


	a = malloc(sizeof(*a));
	a->root = _scan(path, fext);
	return a;

}


void _mark_outdated_recursive(struct AlbumLevel *al, bool thumbs) {
	int i;

	if (!al)
		return;

	for (i = 0; i < al->subalbums; i++) {
		_mark_outdated_recursive(al->subalbum[i].child, thumbs);
		al->subalbum[i].needs_update = true;
	}

	for (i = 0; i < al->pictures; i++) {
		if (thumbs)
			al->picture[i].needs_update = true;
		al->picture[i].variant.needs_update = true;
	}
}


void _locate_outdated(struct AlbumLevel *al, const char *path, const char *fext) {
	char buff[PATH_MAX];
	struct stat st;
	int i;

	for (i = 0; i < al->subalbums; i++) {
		snprintf(buff, PATH_MAX, "%s/%s", path, al->subalbum[i].dirname);
		if (stat(buff, &st) < 0)
			return _mark_outdated_recursive(al, false), _mark_outdated_recursive(al->subalbum[i].child, true);
		_locate_outdated(al, path, fext);
	}

	for (i = 0; i < al->pictures; i++) {
		snprintf(buff, PATH_MAX, "%s/%s%s", path, al->picture[i].fname, fext);
		if (stat(buff, &st) < 0) {
			al->picture[i].variant.needs_update = true;
			goto thumbnail;
		}
		if (al->picture[i].variant.last_update > st.st_mtim.tv_sec)
			al->picture[i].variant.needs_update = true;
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


void album_locate_outdated(struct Album *a, const char *path_target, const char *fext) {
	char buff[PATH_MAX];

	snprintf(buff, PATH_MAX, "%s/%s", path_target, fext);
	_locate_outdated(a->root, buff, fext);
}

void album_locate_obsolete(struct AlbumLevel *al, const char *path_target, bool has_data) {
	DIR *d;
	struct dirent dent, *result;
	struct stat st;
	const char* const nukable_extensions[] = { "_thumb", "_small", ".html", NULL };
	char buff[PATH_MAX];
	int i, j, fnl;

	if (!al)
		return;
	if (!(d = opendir(path_target)))
		return;
	for (readdir_r(d, &dent, &result); result; readdir_r(d, &dent, &result)) {
		if (!(*dent.d_name))
			continue;
		snprintf(buff, PATH_MAX, "%s/%s", path_target, dent.d_name);
		if (stat(buff, &st) < 0)
			continue;
		if (S_ISDIR(st.st_mode)) {
			if (!has_data)
				goto no_name;
			for (i = 0; i < al->subalbums; i++)
				if (!strcmp(al->subalbum[i].dirname, dent.d_name)) {
					album_locate_obsolete(al->subalbum[i].child, buff, true);
					goto has_name;
				}
		no_name:
			album_locate_obsolete(al, buff, false);
		has_name:
			rmdir(buff); /* Will only succeed if the directory is empty */
		} else {
			fnl = strlen(dent.d_name);
			for (j = 0; j < al->pictures; j++) {
				if (has_data)
					if (memcmp(dent.d_name, al->picture[j].fname, strlen(al->picture[j].fname)))
						continue;
				for (i = 0; nukable_extensions[i]; i++) {
					if (fnl < strlen(nukable_extensions[i]))
						continue;
					if (strcmp(dent.d_name + fnl - strlen(nukable_extensions[i]), nukable_extensions[i]))
						continue; /* Can't touch this */
					if (has_data && strlen(al->picture[j].fname) + strlen(nukable_extensions[i]) != fnl)
						continue; /* We nearly nuked the wrong file. Ask before launching! */
					snprintf(buff, PATH_MAX, "%s/%s", path_target, dent.d_name);
					unlink(buff);
					break;
				}
			}
		}
	}
}
