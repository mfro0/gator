/*

    avview preliminary version 
    
    (C) Vladimir Dergachev 2001-2003
    
    
*/

#ifndef __STRING_CACHE_H__
#define __STRING_CACHE_H__

typedef struct {
	long size;
	long string_hash_size;
	long free;
	long mod;
	long mul;
	char **string;
	void **data;
	long *string_hash;
	long *next_string;
	} STRING_CACHE;
	
STRING_CACHE * new_string_cache(void);
long lookup_string(STRING_CACHE *sc, const char * string);
long add_string(STRING_CACHE *sc, char *string);
int valid_id(STRING_CACHE *sc, long string_id);


#endif
