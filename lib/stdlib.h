void memcpy(void *dest, void *src, int n);
void  *memset(void *b, int c, int len);
int strlen(const char *str);
int memcmp(const void *s1, const void *s2, int len);
char *strncpy(char *dst, const char *src, int n);
int strcmp(const char *X, const char *Y);

struct div_t {
  int quot;
  int rem;
} ;

struct ldiv_t {
  long int quot;
  long int rem;
} ;

struct div_t div(int numer, int denom);
struct ldiv_t ldiv(long int numer, long int denom);
char * strcpy(char *strDest, const char *strSrc);

void emptyBuffer(char* buf,unsigned int l);
