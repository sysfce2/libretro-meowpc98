#if defined(__LIBRETRO__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE	/* fopencookie */
#endif

#include "compiler.h"

#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "codecnv/codecnv.h"
#include "dosio.h"


static OEMCHAR curpath[MAX_PATH];
static OEMCHAR *curfilep = curpath;

#define ISKANJI(c)	((((c) - 0xa1) & 0xff) < 0x5c)

#if defined(VITA)
#  include <psp2/io/fcntl.h>
#  include <psp2/io/dirent.h>
#  include <psp2/io/stat.h>
#elif defined(PSP)
#  include <pspiofilemgr.h>
#endif


#if defined(VITA) || defined(PSP)
#define mkdir sceIoMkdir
#endif


#if defined(__LIBRETRO__)
#include "libretro.h"

static struct retro_vfs_interface *vfs_iface;

void
dosio_set_vfs_interface(struct retro_vfs_interface *iface)
{

	vfs_iface = iface;
}

/* A path the frontend hands over can be one only the frontend can open:
 * Android's Storage Access Framework uses content:// URIs, which no C library
 * resolves. FILEH is a FILE * all over the emulator, so rather than converting
 * every caller, wrap the frontend's file handle in a FILE *. */
#if defined(__BIONIC__) || defined(__ANDROID__)
#define NP2_HAVE_VFS_FILE 1
/* funopen64 only exists from API 24 on; below that funopen is all there is,
 * and its offsets are as wide as off_t. */
#if defined(__ANDROID_API__) && __ANDROID_API__ >= 24
typedef fpos64_t	vfs_fpos_t;
#define VFS_FUNOPEN	funopen64
#else
typedef fpos_t		vfs_fpos_t;
#define VFS_FUNOPEN	funopen
#endif

static int
vfs_read_cb(void *c, char *buf, int size)
{
	int64_t got = vfs_iface->read((struct retro_vfs_file_handle *)c, buf, (uint64_t)size);
	return (got < 0 ? -1 : (int)got);
}

static int
vfs_write_cb(void *c, const char *buf, int size)
{
	int64_t put = vfs_iface->write((struct retro_vfs_file_handle *)c, buf, (uint64_t)size);
	return (put < 0 ? -1 : (int)put);
}

static vfs_fpos_t
vfs_seek_cb(void *c, vfs_fpos_t off, int whence)
{
	int pos = (whence == SEEK_SET ? RETRO_VFS_SEEK_POSITION_START :
	           whence == SEEK_CUR ? RETRO_VFS_SEEK_POSITION_CURRENT :
	                                RETRO_VFS_SEEK_POSITION_END);

	/* The seek return value is 0 on success in some frontends and the new
	 * offset in others, so ask tell() for the position instead. */
	if (vfs_iface->seek((struct retro_vfs_file_handle *)c, (int64_t)off, pos) < 0)
		return (vfs_fpos_t)-1;
	return (vfs_fpos_t)vfs_iface->tell((struct retro_vfs_file_handle *)c);
}

static int
vfs_close_cb(void *c)
{

	return vfs_iface->close((struct retro_vfs_file_handle *)c);
}

static FILE *
vfs_wrap(struct retro_vfs_file_handle *h)
{
	FILE *f = VFS_FUNOPEN(h, vfs_read_cb, vfs_write_cb, vfs_seek_cb, vfs_close_cb);

	if (f == NULL)
		vfs_iface->close(h);
	return f;
}
#elif defined(__GLIBC__)
#define NP2_HAVE_VFS_FILE 1

static ssize_t
vfs_read_cb(void *c, char *buf, size_t size)
{
	int64_t got = vfs_iface->read((struct retro_vfs_file_handle *)c, buf, (uint64_t)size);
	return (got < 0 ? -1 : (ssize_t)got);
}

static ssize_t
vfs_write_cb(void *c, const char *buf, size_t size)
{
	int64_t put = vfs_iface->write((struct retro_vfs_file_handle *)c, buf, (uint64_t)size);
	return (put < 0 ? -1 : (ssize_t)put);
}

static int
vfs_seek_cb(void *c, off64_t *off, int whence)
{
	int pos = (whence == SEEK_SET ? RETRO_VFS_SEEK_POSITION_START :
	           whence == SEEK_CUR ? RETRO_VFS_SEEK_POSITION_CURRENT :
	                                RETRO_VFS_SEEK_POSITION_END);
	int64_t at;

	/* See the note in the bionic version above. */
	if (vfs_iface->seek((struct retro_vfs_file_handle *)c, (int64_t)*off, pos) < 0)
		return -1;
	at = vfs_iface->tell((struct retro_vfs_file_handle *)c);
	if (at < 0)
		return -1;
	*off = (off64_t)at;
	return 0;
}

static int
vfs_close_cb(void *c)
{

	return vfs_iface->close((struct retro_vfs_file_handle *)c);
}

static FILE *
vfs_wrap(struct retro_vfs_file_handle *h)
{
	cookie_io_functions_t fns = { vfs_read_cb, vfs_write_cb, vfs_seek_cb, vfs_close_cb };
	FILE *f = fopencookie(h, "r+", fns);

	if (f == NULL)
		vfs_iface->close(h);
	return f;
}
#endif	/* bionic / glibc */

static FILEH
vfs_open(const OEMCHAR *path, const char *mode)
{
#if defined(NP2_HAVE_VFS_FILE)
	struct retro_vfs_file_handle *h;
	unsigned access;

	if (vfs_iface == NULL || strstr(path, "://") == NULL)
		return NULL;

	access = (strchr(mode, 'w') != NULL) ? RETRO_VFS_FILE_ACCESS_WRITE :
	         (strchr(mode, '+') != NULL) ? (RETRO_VFS_FILE_ACCESS_READ_WRITE |
	                                        RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING) :
	                                       RETRO_VFS_FILE_ACCESS_READ;
	h = vfs_iface->open(path, access, RETRO_VFS_FILE_ACCESS_HINT_NONE);
	if (h == NULL)
		return NULL;
	return vfs_wrap(h);
#else
	(void)path;
	(void)mode;
	return NULL;
#endif
}
#endif	/* __LIBRETRO__ */


void
dosio_init(void)
{

	/* nothing to do */
}

void
dosio_term(void)
{

	/* nothing to do */
}

/* ファイル操作 */
FILEH
file_open(const OEMCHAR *path)
{
	FILEH fh;

#if defined(__LIBRETRO__)
	fh = vfs_open(path, "rb+");
	if (fh)
		return fh;
	fh = vfs_open(path, "rb");
	if (fh)
		return fh;
#endif
	fh = fopen(path, "rb+");
	if (fh)
		return fh;
	return fopen(path, "rb");
}

FILEH
file_open_rb(const OEMCHAR *path)
{
#if defined(__LIBRETRO__)
	FILEH fh = vfs_open(path, "rb");

	if (fh)
		return fh;
#endif
	return fopen(path, "rb");
}

FILEH
file_create(const OEMCHAR *path)
{
#if defined(__LIBRETRO__)
	FILEH fh = vfs_open(path, "wb+");

	if (fh)
		return fh;
#endif
	return fopen(path, "wb+");
}

long
file_seek(FILEH handle, long pointer, int method)
{

	fseek(handle, pointer, method);
	return ftell(handle);
}

UINT
file_read(FILEH handle, void *data, UINT length)
{

	return (UINT)fread(data, 1, length, handle);
}

UINT
file_write(FILEH handle, const void *data, UINT length)
{

	return (UINT)fwrite(data, 1, length, handle);
}

short
file_close(FILEH handle)
{

	fclose(handle);
	return 0;
}

UINT
file_getsize(FILEH handle)
{
	struct stat sb;

#if defined(NP2_HAVE_VFS_FILE)
	/* A VFS-backed stream has no file descriptor for fstat() to work from,
	 * so measure it by seeking instead. */
	if (fileno(handle) < 0) {
		long cur = ftell(handle);
		long end;

		if ((cur < 0) || (fseek(handle, 0, SEEK_END) != 0))
			return 0;
		end = ftell(handle);
		fseek(handle, cur, SEEK_SET);
		return (end < 0) ? 0 : (UINT)end;
	}
#endif
	if (fstat(fileno(handle), &sb) == 0)
		return sb.st_size;
	return 0;
}

short
file_attr(const OEMCHAR *path)
{
	struct stat sb;
	short attr;

	if (stat(path, &sb) == 0) {
		if (S_ISDIR(sb.st_mode)) {
			return FILEATTR_DIRECTORY;
		}
		attr = 0;
		if (!(sb.st_mode & S_IWUSR)) {
			attr |= FILEATTR_READONLY;
		}
		return attr;
	}
	return -1;
}

static BRESULT
cnvdatetime(struct stat *sb, DOSDATE *dosdate, DOSTIME *dostime)
{
	struct tm *ftime;

	ftime = localtime(&sb->st_mtime);
	if (ftime) {
		if (dosdate) {
			dosdate->year = ftime->tm_year + 1900;
			dosdate->month = ftime->tm_mon + 1;
			dosdate->day = ftime->tm_mday;
		}
		if (dostime) {
			dostime->hour = ftime->tm_hour;
			dostime->minute = ftime->tm_min;
			dostime->second = ftime->tm_sec;
		}
		return SUCCESS;
	}
	return FAILURE;
}

short
file_getdatetime(FILEH handle, DOSDATE *dosdate, DOSTIME *dostime)
{
	struct stat sb;

#if defined(NP2_HAVE_VFS_FILE)
	/* No descriptor to stat, and the frontend's VFS has no timestamps to
	 * offer either - hand back a fixed date rather than an error. */
	if (fileno(handle) < 0) {
		if (dosdate) {
			dosdate->year = 2000;
			dosdate->month = 1;
			dosdate->day = 1;
		}
		if (dostime) {
			dostime->hour = 0;
			dostime->minute = 0;
			dostime->second = 0;
		}
		return 0;
	}
#endif
	if ((fstat(fileno(handle), &sb) == 0)
	 && (cnvdatetime(&sb, dosdate, dostime) == SUCCESS))
		return 0;
	return -1;
}

short
file_delete(const OEMCHAR *path)
{

	return (short)unlink(path);
}

short
file_dircreate(const OEMCHAR *path)
{
#if defined(WIN32)
	return((short)mkdir(path));
#else
	return (short)mkdir(path, 0777);
#endif
}


/* カレントファイル操作 */
void
file_setcd(const OEMCHAR *exepath)
{

	milstr_ncpy(curpath, exepath, sizeof(curpath));
	curfilep = file_getname(curpath);
	*curfilep = '\0';
}

char *
file_getcd(const OEMCHAR *filename)
{

	*curfilep = '\0';
	file_catname(curpath, filename, sizeof(curpath));
	return curpath;
}

FILEH
file_open_c(const OEMCHAR *filename)
{

	*curfilep = '\0';
	file_catname(curpath, filename, sizeof(curpath));
	return file_open(curpath);
}

FILEH
file_open_rb_c(const OEMCHAR *filename)
{

	*curfilep = '\0';
	file_catname(curpath, filename, sizeof(curpath));
	return file_open_rb(curpath);
}

FILEH
file_create_c(const OEMCHAR *filename)
{

	*curfilep = '\0';
	file_catname(curpath, filename, sizeof(curpath));
	return file_create(curpath);
}

short
file_delete_c(const OEMCHAR *filename)
{

	*curfilep = '\0';
	file_catname(curpath, filename, sizeof(curpath));
	return file_delete(curpath);
}

short
file_attr_c(const OEMCHAR *filename)
{

	*curfilep = '\0';
	file_catname(curpath, filename, sizeof(curpath));
	return file_attr(curpath);
}

FLISTH
file_list1st(const OEMCHAR *dir, FLINFO *fli)
{
	FLISTH ret;

	ret = (FLISTH)_MALLOC(sizeof(_FLISTH), "FLISTH");
	if (ret == NULL) {
		VERBOSE(("file_list1st: couldn't alloc memory (size = %d)", sizeof(_FLISTH)));
		return FLISTH_INVALID;
	}

	milstr_ncpy(ret->path, dir, sizeof(ret->path));
	file_setseparator(ret->path, sizeof(ret->path));
	ret->hdl = opendir(ret->path);
	VERBOSE(("file_list1st: opendir(%s)", ret->path));
	if (ret->hdl == NULL) {
		VERBOSE(("file_list1st: opendir failure"));
		_MFREE(ret);
		return FLISTH_INVALID;
	}
	if (file_listnext((FLISTH)ret, fli) == SUCCESS) {
		return (FLISTH)ret;
	}
	VERBOSE(("file_list1st: file_listnext failure"));
	closedir(ret->hdl);
	_MFREE(ret);
	return FLISTH_INVALID;
}

BRESULT
file_listnext(FLISTH hdl, FLINFO *fli)
{
	OEMCHAR buf[MAX_PATH];
	struct dirent *de;
	struct stat sb;

	de = readdir(hdl->hdl);
	if (de == NULL) {
		VERBOSE(("file_listnext: readdir failure"));
		return FAILURE;
	}

	milstr_ncpy(buf, hdl->path, sizeof(buf));
	milstr_ncat(buf, de->d_name, sizeof(buf));
	if (stat(buf, &sb) != 0) {
		VERBOSE(("file_listnext: stat failure. (path = %s)", buf));
		return FAILURE;
	}

	fli->caps = FLICAPS_SIZE | FLICAPS_ATTR | FLICAPS_DATE | FLICAPS_TIME;
	fli->size = sb.st_size;
	fli->attr = 0;
	if (S_ISDIR(sb.st_mode)) {
		fli->attr |= FILEATTR_DIRECTORY;
	}
	if (!(sb.st_mode & S_IWUSR)) {
		fli->attr |= FILEATTR_READONLY;
	}
	cnvdatetime(&sb, &fli->date, &fli->time);
	milstr_ncpy(fli->path, de->d_name, sizeof(fli->path));
	VERBOSE(("file_listnext: success"));
	return SUCCESS;
}

void
file_listclose(FLISTH hdl)
{

	if (hdl) {
		closedir(hdl->hdl);
		_MFREE(hdl);
	}
}

static int
euckanji1st(const OEMCHAR *str, int pos)
{
	int ret;
	int c;

	for (ret = 0; pos >= 0; ret ^= 1) {
		c = (UINT8)str[pos--];
		if (!ISKANJI(c))
			break;
	}
	return ret;
}

void
file_cpyname(OEMCHAR *dst, const OEMCHAR *src, int maxlen)
{
	int i;

	if (maxlen-- > 0) {
		for (i = 0; i < maxlen && src[i] != '\0'; i++) {
			dst[i] = src[i];
		}
		if (i > 0) {
			if (euckanji1st(src, i-1)) {
				i--;
			}
		}
		dst[i] = '\0';
	}
}

void
file_catname(OEMCHAR *path, const OEMCHAR *filename, int maxlen)
{

	for (; maxlen > 0; path++, maxlen--) {
		if (*path == '\0') {
			break;
		}
	}
	if (maxlen > 0) {
		milstr_ncpy(path, filename, maxlen);
		for (; *path != '\0'; path++) {
			if (!ISKANJI(*path)) {
				path++;
				if (*path == '\0') {
					break;
				}
			} else if (((*path - 0x41) & 0xff) < 26) {
				*path |= 0x20;
			} else if (*path == '\\') {
				*path = G_DIR_SEPARATOR;
			}
		}
	}
}

BOOL
file_cmpname(const OEMCHAR *path, const OEMCHAR *path2)
{

	return strcasecmp(path, path2);
}

OEMCHAR *
file_getname(const OEMCHAR *path)
{
	const OEMCHAR *ret;

	for (ret = path; *path != '\0'; path++) {
		if (ISKANJI(*path)) {
			path++;
			if (*path == '\0') {
				break;
			}
		} else if (*path == G_DIR_SEPARATOR) {
			ret = path + 1;
		}
	}
	return (OEMCHAR *)ret;
}

void
file_cutname(OEMCHAR *path)
{
	OEMCHAR *p;

	p = file_getname(path);
	*p = '\0';
}

OEMCHAR *
file_getext(const OEMCHAR *path)
{
	const OEMCHAR *p, *q;

	for (p = file_getname(path), q = NULL; *p != '\0'; p++) {
		if (*p == '.') {
			q = p + 1;
		}
	}
	if (q == NULL) {
		q = p;
	}
	return (OEMCHAR *)q;
}

void
file_cutext(OEMCHAR *path)
{
	OEMCHAR *p, *q;

	for (p = file_getname(path), q = NULL; *p != '\0'; p++) {
		if (*p == '.') {
			q = p;
		}
	}
	if (q != NULL) {
		*q = '\0';
	}
}

void
file_cutseparator(OEMCHAR *path)
{
	int pos;

	pos = strlen(path) - 1;
	if ((pos > 0) && (path[pos] == G_DIR_SEPARATOR)) {
		path[pos] = '\0';
	}
}

void
file_setseparator(OEMCHAR *path, int maxlen)
{
	int pos;

	pos = strlen(path);
	if ((pos) && (path[pos-1] != G_DIR_SEPARATOR) && ((pos + 2) < maxlen)) {
		path[pos++] = G_DIR_SEPARATOR;
		path[pos] = '\0';
	}
}
