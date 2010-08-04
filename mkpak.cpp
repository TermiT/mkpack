#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500
#endif
#include <ftw.h>
#include <CPVRTexture.h>
using namespace pvrtexlib;
#include <sndfile.hh>

typedef unsigned char *byte;
#include "ipak.h"

/* cut from ipak.c */
/*
 ==================
 PK_HashName
 
 ==================
 */
int PK_HashName( const char *name, char canonical[MAX_PK_NAME] ) {
	int	o = 0;
	int	hash = 0;
	
	do {
		int c = name[o];
		if ( c == 0 ) {
			break;
		}
		// backslashes to forward slashes
		if ( c == '\\' ) {
			c = '/';
		}
		// to lowercase
		c = tolower( c );
		canonical[o++] = c;
		hash = (hash << 5) - hash + c;
	} while ( o < MAX_PK_NAME-1 );
	canonical[o] = 0;
	
	return hash;
}

/* iPack memory limit */
#define MAX_IPAK_SIZE 1024*1024*100 /* 100 Mb */

char *basedir;		/* directory to pack */
size_t basedirlen;	/* length of basedir string */
char *outfile;		/* output file name */

void *mem;					/* preallocated memory */
pkHeader_t *header = NULL;	/* pointer to header */
size_t cursize = 0;			/* amount of memmory used for iPack content */

pkWavData_t *wavdata_p = NULL;		/* pointer to first wav data struct */
pkTextureData_t *texdata_p = NULL;	/* pointer to first texture data struct */

/* close all files and free all memory */
void cleanup() {
	free(header);
	fcloseall();
}

/* return slice of preallocated memory */
void *memextend(size_t size) {
    void *r;
    r = (char*)mem + cursize;
    cursize += size;
    return r;
}

void mktype(pkType_t *t) {
    t->count = 0;
    memset(t->hashChains, -1, sizeof(t->hashChains));
    t->structSize = 0;
    t->tableOfs = -1;
}

void mkhashchain(pkType_t *t) {
    pkName_t *name;
    int next;
    
    name = (pkName_t*)((char*)header+t->tableOfs);
    for (int i = 0; i < t->count; i++) {
        name->nextOnHashChain = t->hashChains[name->nameHash&(PK_HASH_CHAINS-1)];
        t->hashChains[name->nameHash&(PK_HASH_CHAINS-1)] = i;
        name = (pkName_t*)(((char*)name)+t->structSize);
    }
}

void mkheader() {
	header = (pkHeader_t*)memextend(sizeof(pkHeader_t));
    header->version = PKFILE_VERSION;
    mktype(&header->raws);
    mktype(&header->textures);
    mktype(&header->wavs);
}

void mkname(pkName_t *name, const char *strname) {
    name->nameHash = PK_HashName(strname, name->name);
    name->nextOnHashChain = -1;
}

char* mkpath(char *name, const char *suffix) {
    static char b[512];
    sprintf(b, "%s/%s%s", basedir, name, suffix);
    return b;
}

/* find file extension */
template<typename T>
static T* findext(T* filename) {
	T *dot;
	dot = NULL;
	for (T *c = filename; *c; c++) {
		if (*c == '.') {
			dot = c;
		}
	}
	return dot;
}

/* add wavdata struct */
void addwav(const char *path) {
	pkWavData_t *wavdata;	
	wavdata = (pkWavData_t*)memextend(sizeof(pkWavData_t));
	if (wavdata_p == NULL) {
		wavdata_p = wavdata;
		header->wavs.count = 0;
		header->wavs.structSize = sizeof(pkWavData_t);
		header->wavs.tableOfs = ((char*)wavdata_p)-((char*)header);
	}
	header->wavs.count++;
	mkname(&wavdata->name, path+basedirlen+1);
}

/* add texturedata struct */
void addtex(const char *path) {
	pkTextureData_t *texdata;	
	texdata = (pkTextureData_t*)memextend(sizeof(pkTextureData_t));
	if (texdata_p == NULL) {
		texdata_p = texdata;
		header->textures.count = 0;
		header->textures.structSize = sizeof(pkTextureData_t);
		header->textures.tableOfs = ((char*)texdata_p)-((char*)mem);
	}
	header->textures.count++;
	mkname(&texdata->name, path+basedirlen+1);
}

/* find wavs */
static int collect_wavs(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf) {
	char *resname;
	char *ext;
	if (tflag != FTW_F) {
		return 0;
	}
	resname = strdup(fpath+basedirlen+1);
	ext = findext(resname)+1;
	if (!strcmp(ext, "wav")) {
		addwav(fpath);
	}
	free(resname);	
	return 0;
}

/* find textures */
static int collect_textures(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf) {
	char *resname;
	char *ext;
	if (tflag != FTW_F) {
		return 0;
	}
	resname = strdup(fpath+basedirlen+1);
	ext = findext(resname)+1;
	if (!strcmp(ext, "pvr")) {
		printf("%s : add\n", resname);
		addtex(fpath);
	}
	free(resname);	
	return 0;
}

void writepak() {
	FILE *f;   
	f = fopen(outfile, "wb+");
	if (f == NULL) {
		printf("Cannot open file for writing: %s\n", outfile);
		exit(1);
	}
	fwrite(mem, 1, cursize, f);
	fclose(f);
}

void loadwavbits(pkWavData_t *wd) {	
	printf("Loading wav data: %s\n", wd->name.name);
	SndfileHandle sf(mkpath(wd->name.name, ""));
	if (!sf) {
		printf("Cannot load wav file: %s\n", mkpath(wd->name.name, ""));
	}
	wd->wavChannels = sf.channels();
	wd->wavChannelBytes = 2;
	wd->wavRate = sf.samplerate();
	wd->wavNumSamples = (int)sf.frames();
	short *wavmem = (short*)memextend(wd->wavChannels*wd->wavChannelBytes*wd->wavNumSamples);
	sf.read(wavmem, sf.frames());
	wd->wavDataOfs = (char*)wavmem - (char*)mem;
}

int ParseTextureFormatString(const char *fmtstr) {
	if (!strcmp(fmtstr, "565")) return TF_565;
	if (!strcmp(fmtstr, "5551")) return TF_5551;
	if (!strcmp(fmtstr, "4444")) return TF_4444;
	if (!strcmp(fmtstr, "8888")) return TF_8888;
	if (!strcmp(fmtstr, "LA")) return TF_LA;
	if (!strcmp(fmtstr, "PVR4")) return TF_PVR4;
	if (!strcmp(fmtstr, "PVR4A")) return TF_PVR4A;
	if (!strcmp(fmtstr, "PVR2")) return TF_PVR2;
	if (!strcmp(fmtstr, "PVR2A")) return TF_PVR2A;
	printf("Unknown texture format: %s\n", fmtstr);
	exit(1);
}

void readbounds(FILE *f, pkTextureData_t *td) {
	static char b[100], *p;
	fgets(b, 100, f);
	if (!strncmp(b, "bounds=\"", strlen("bounds=\""))) {
		p = b+strlen("bounds=\"");
		sscanf(p, "%d %d %d %d %d %d %d %d",
			&td->bounds[0][0][0], &td->bounds[0][0][1], &td->bounds[0][1][0], &td->bounds[0][1][1], 
			&td->bounds[1][0][0], &td->bounds[1][0][1], &td->bounds[1][1][0], &td->bounds[1][1][1]);
	}
}

void loadtexturecfg(pkTextureData_t *td) {
	static char path[512], format[32];
	FILE *f;
	sprintf(path, "%s/%s", basedir, td->name.name);
	strcpy(findext(path), ".cfg");
	printf("Loading texture config: %s\n", path);
	if ((f = fopen(path, "r")) != NULL) {
		fscanf(f, "format=%s\n", format);
		td->format = ParseTextureFormatString(format);
		fscanf(f, "uploadWidth=%d\n", &td->uploadWidth);
		fscanf(f, "uploadHeight=%d\n", &td->uploadHeight);
		fscanf(f, "numLevels=%d\n", &td->numLevels);
		fscanf(f, "wrapS=%d\n", &td->wrapS);
		fscanf(f, "wrapT=%d\n", &td->wrapT);
		fscanf(f, "minFilter=%d\n", &td->minFilter);
		fscanf(f, "magFilter=%d\n", &td->magFilter);
		fscanf(f, "aniso=%d\n", &td->aniso);
		fscanf(f, "srcWidth=%d\n", &td->srcWidth);
		fscanf(f, "srcHeight=%d\n", &td->srcHeight);
		fscanf(f, "maxS=%f\n", &td->maxS);
		fscanf(f, "maxT=%f\n", &td->maxT);
		fscanf(f, "numBounds=%d\n", &td->numBounds);
		readbounds(f, td);
		fclose(f);
		printf("Config loaded\n");
	} else {
		printf("Cannot open config file, using default parameters\n");
	}
}

void loadtexturebits(pkTextureData_t *td) {
	static char path[512], oldname[MAX_PK_NAME];
	sprintf(path, "%s/%s", basedir, td->name.name);
	strcpy(oldname, td->name.name);
	strcpy(findext(oldname), ".tga");
	mkname(&td->name, oldname);
	loadtexturecfg(td);
	printf("Loading texture data: %s\n", td->name.name);
	CPVRTexture tex(path);
	PixelType pt = tex.getHeader().getPixelType();
	switch (pt) {
		case OGL_RGB_565:		td->format = TF_565; break;
		case OGL_RGBA_5551: 	td->format = TF_5551; break;
		case OGL_RGBA_4444:		td->format = TF_4444; break;
		case OGL_RGBA_8888:		td->format = TF_8888; break;
		case OGL_BGRA_8888:		td->format = TF_8888; break;
		case OGL_AI_88:			td->format = TF_LA; break;
		case OGL_PVRTC4:		td->format = tex.getHeader().hasAlpha() ? TF_PVR4A : TF_PVR4; break;
		case OGL_PVRTC2:		td->format = tex.getHeader().hasAlpha() ? TF_PVR2A : TF_PVR2; break;
		default:
			printf("This PVR pixel format is unsupported by Doom Classic engine\n");
			exit(1);
	}
	td->uploadWidth = tex.getWidth();
	td->uploadHeight = tex.getHeight();
	td->numLevels = tex.getMipMapCount()+1;
	/*
	td->srcWidth = td->srcWidth;
	td->srcHeight = td->srcHeight;
	*/
	void *texmem = memextend(tex.getData().getDataSize());
	memcpy(texmem, tex.getData().getData(), tex.getData().getDataSize());
	td->picDataOfs = (char*)texmem - (char*)mem;
}

void loadallwavs() {
	pkWavData_t *wd;
	wd = wavdata_p;
	if (wd == NULL) return;
	for (int i = 0; i < header->wavs.count; i++) {
		loadwavbits(wd);
		wd++;
	}
}

void loadalltextures() {
	pkTextureData_t *td;
	td = texdata_p;
	if (td == NULL) return;
	for (int i = 0; i < header->textures.count; i++) {
		loadtexturebits(td);
		td++;
	}
}

int main(int argc, char *argv[]) {
	printf("mkpak tool by General Arcade\n\n");
	if (argc != 3) {
		printf("Usage: mkpak <basedir> <pakfile>\n");
		exit(1);
	}
	basedir = argv[1];
	basedirlen = strlen(basedir);
	outfile = argv[2];
	printf("Scanning directory...\n");
	mem = malloc(MAX_IPAK_SIZE);
	mkheader();
	atexit(cleanup);	
	nftw(basedir, collect_wavs, 20, 0);
	nftw(basedir, collect_textures, 20, 0);
	printf("Found %d wavs, %d textures\n", header->wavs.count, header->textures.count);
	printf("Loading data...\n");
	loadallwavs();
	loadalltextures();
	mkhashchain(&header->wavs);
	mkhashchain(&header->textures);
	writepak();
	printf("Done\n");
	return 0;
}
