// Copyright (C) 2019 Joel Rosdahl
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc., 51
// Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#include "ccache.h"

#include "common_header.h"
#include "manifest.h"
#include "result.h"

static size_t
content_size(
	const char *path, const char *magic, uint8_t version, bool *is_compressed)
{
	char *errmsg;
	size_t size = 0;
	FILE *f = fopen(path, "rb");
	if (!f) {
		cc_log("Failed to open %s for reading: %s", path, strerror(errno));
		goto error;
	}
	struct common_header header;
	if (!common_header_initialize_for_reading(
		    &header,
		    f,
		    magic,
		    version,
		    NULL,
		    NULL,
		    NULL,
		    &errmsg)) {
		cc_log("Error: %s", errmsg);
		goto error;
	}
	size = common_header_content_size(&header, is_compressed);

error:
	if (f) {
		fclose(f);
	}
	return size;
}

static unsigned num_files;
static unsigned comp_files;

static uint64_t cache_size;
static uint64_t real_size;

// This measures the size of files in the cache.
static void
measure_fn(const char *fname, struct stat *st)
{
	if (!S_ISREG(st->st_mode)) {
		return;
	}

	char *p = basename(fname);
	if (str_eq(p, "stats")) {
		goto out;
	}

	if (str_startswith(p, ".nfs")) {
		// Ignore temporary NFS files that may be left for open but deleted files.
		goto out;
	}

	if (strstr(p, "CACHEDIR.TAG")) {
		goto out;
	}

	size_t uncompressed_size;
	bool is_compressed;
	const char *file_ext = get_extension(p);
	if (str_eq(file_ext, ".manifest")) {
		uncompressed_size =
			content_size(fname, MANIFEST_MAGIC, MANIFEST_VERSION, &is_compressed);
	} else if (str_eq(file_ext, ".result")) {
		uncompressed_size =
			content_size(fname, RESULT_MAGIC, RESULT_VERSION, &is_compressed);
	} else {
		uncompressed_size = 0;
		is_compressed = false;
	}

	// Ignore unknown files in the cache, including any files from older
	// versions.
	if (uncompressed_size > 0) {
		cache_size += st->st_size;
		num_files++;
		if (is_compressed) {
			real_size += uncompressed_size;
			comp_files++;
		} else {
			real_size += st->st_size;
		}
	}

out:
	free(p);
}

// Process up all cache subdirectories.
void compress_stats(struct conf *conf)
{
	num_files = 0;
	comp_files = 0;
	cache_size = 0;
	real_size = 0;

	for (int i = 0; i <= 0xF; i++) {
		char *dname = format("%s/%1x", conf->cache_dir, i);
		traverse(dname, measure_fn);
		free(dname);
	}

	char *cache_str = format_human_readable_size(cache_size);
	printf("Compressed size: %s, %.0f files\n",
	       cache_str, (double)comp_files);
	free(cache_str);
	char *real_str = format_human_readable_size(real_size);
	printf("Uncompressed size: %s, %.0f files\n",
	       real_str, (double)num_files);
	free(real_str);

	double percent = real_size > 0 ? (100.0 * comp_files) / num_files : 0.0;
	printf("Compressed files: %.2f %%\n", percent);
	double ratio = cache_size > 0 ? ((double) real_size) / cache_size : 0.0;
	double savings = ratio > 0.0 ? 100.0 - (100.0 / ratio) : 0.0;
	printf("Compression ratio: %.2f %% (%.1fx)\n", savings, ratio);
}

