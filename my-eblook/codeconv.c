/*
 * codeconv.c
 * Copyright(c) 2001 Takashi NEMOTO
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. 
 *
 * Written by Takashi Nemoto (tnemoto@mvi.biglobe.ne.jp).
 * Modified by Kazuhiko <kazuhiko@ring.gr.jp>
 * Modified by Satomi <satomi@ring.gr.jp>
 *
 */

/* #define DEBUG_CODECONV */

#include "config.h"

#include "codeconv.h"

#ifdef HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif

#include <stdio.h>

#ifdef HAVE_STDLIB_H
#  include <stdlib.h>
#endif

#ifdef HAVE_LOCALE_H
#  include <locale.h>
#endif

#ifdef HAVE_ICONV_H
#  include <iconv.h>
#endif

#ifdef HAVE_ERRNO_H
#  include <errno.h>
#endif

#ifdef HAVE_STRING_H
#  include <string.h>
#endif

#ifdef HAVE_LANGINFO_H
#include <langinfo.h>
#endif

#ifndef HAVE_MEMCPY
#define memcpy(d, s, n) bcopy((s), (d), (n))
#ifdef __STDC__
void *memchr(const void *, int, size_t);
int memcmp(const void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
#else /* not __STDC__ */
char *memchr();
int memcmp();
char *memmove();
char *memset();
#endif /* not __STDC__ */
#endif

#ifdef HAVE_ICONV
static iconv_t cur_to_euc = (iconv_t)-1;
static iconv_t euc_to_cur = (iconv_t)-1;
static const char *eucjp_code_name = NULL;
#endif

/* Return code <0: error, -2: Output Buffer Overflow */
size_t current_to_euc PROTO((char **current, size_t *in_len,
			     char **euc, size_t *out_len));
static size_t euc_to_current PROTO((char **euc, size_t *in_len,
				   char **current, size_t *out_len));


enum CONV_MODE {IO_AUTO, IO_ICONV, IO_SJIS, IO_EUC} conv_mode;

static enum CONV_MODE detect_conv_mode PROTO((const char *encoding));
     



#define TMP_SIZE 10240

static int	xputs_raw	PROTO((const char *str, int len, FILE *fp));
static int	xputs2		PROTO((const char *str, int len, FILE *fp));

static const char *euc_jp_names[] = {
    "eucJP", "EUC-JP", "eucjp", "euc-jp", "EUCJP", "ujis", "UJIS",
    "euc", "EUC", NULL
};

static const char *shift_jis_names[] = {
    "SHIFT-JIS", "SHIFT_JIS", "SJIS", "CSSHIFTJIS", "SHIFTJIS", NULL
};

#ifdef HAVE_ICONV
static const char *iso_2022_jp_names[] = {
  "ISO-2022-JP-3", "ISO-2022-JP-2", "ISO-2022-JP", 
  "CSISO2022JP", "CSISO2022JP2", "CSISO2022JP3", 
  "ISO-2022", "ISO2022", "ISO2022JP", "ISO2022-JP", "JIS", NULL
};

static const char *japanese_names[] = {
    "ja", "japanese", NULL
};
#endif

static int match_str(str,str_list)
     const char *str;
     const char **str_list;
{
    const char **ptr;
    for(ptr=str_list;*ptr!=NULL;ptr++) {
      if (strcasecmp(str,*ptr)==0) return 1;
    }
    return 0;
}

/* 

 �������Ѵ��ؿ��������� 

 
 1. encoding �����ꤵ��Ƥ����顢�ޤ����ꤵ�줿 encoding ��
   1a. �ޤ� ���ꤵ�줿 encoding �� EUC/SJIS ���ɤ�����Ƚ�� => IO_EUC / IO_SJIS
   1b. ����ʤ� iconv ���Ѵ��Ǥ��� encoding ����Ƚ��       => IO_ICONV
   �ʲ�Ʊ�͡�

 2. ���˽�����Ѥߤ��ä��� �����ǽ�λ

 3. ����ʤ� locale ����μ������ߤ�
   3a. nl_langinfo(CODESET) ����� encoding ���Ф����ߤ롣
   3b. LC_CTYPE ����� encoding ���Ф����ߤ롣
   2e. LC_CTYPE ��Ⱦ��(.�ʹ�) ����� encoding ���Ф����ߤ롣

 3. FALLBACK_ENCODING ��

 4. �������� EUC_JP 

   ����ˤʤ���
   SJIS �� locale ̾�� ja/japanese �ξ��
          iconv ��ͭ���� => EUC_JP �� iconv ̾�ϡ�
	  iconv ��̵����� => 4. �� EUC_JP ���ʡ�
   locale �� C �Ȥ� en_US ���ä��顩 => ���� FALLBACK ���롣
        gettext �б����ξ����׼�ľ����

 */

#ifdef HAVE_ICONV

static int 
setup_eucjp_code_name()
{
    const char **enc;
    iconv_t ic;
    if (eucjp_code_name == NULL) {
        for (enc = euc_jp_names; *enc != NULL; enc++) {
	    ic = iconv_open(*enc, *enc);
	    if (ic != (iconv_t)-1) {
	        eucjp_code_name = *enc;
		iconv_close(ic);
		break;
	    }
	}
	if (eucjp_code_name == NULL) {
	    /* EUC-JP ����˼��� - ۣ��� "ja" "japanese" �� */
	    for (enc = japanese_names; *enc != NULL; enc++) {
	        ic = iconv_open(*enc,*enc);
		if (ic != (iconv_t)-1) {
		    eucjp_code_name = *enc;
		    iconv_close(ic);
		    break;
		}
	    }
	}
    }
    return eucjp_code_name != NULL;
}

/* Current locale �� codeset �����ܸ줬�����뤫��
   ���Ԥʤ� 0, �����ʤ� 1 ���֤� */

static int 
iconv_test(ctoe, etoc)
     iconv_t ctoe, etoc;
{
  /* ʸ���� "�¸�" */
#define TEST_STRING "\xBC\xC2\xB8\xB3"
#define TEST_LENGTH 50
    char test1_0[TEST_LENGTH],test2_0[TEST_LENGTH],test3_0[TEST_LENGTH];
    char *test1,*test2,*test3;
    size_t ilen,olen;

    if (ctoe == (iconv_t)-1 || etoc == (iconv_t)-1) 
        return 0;
    strcpy(test1_0,TEST_STRING); 
    test1=test1_0;
    test2=test2_0;
    test3=test3_0;
    ilen=strlen(TEST_STRING);
    olen=TEST_LENGTH;

    /* euc-jp => current code ���Ѵ��ƥ��� */
    if (iconv(etoc,&test1,&ilen,&test2,&olen) == ((size_t)-1))
        return 0;
    if (iconv(etoc,NULL,&ilen,&test2,&olen) == ((size_t)-1))
        return 0;

    /* current code ���� ������뤫 */
    test2=test2_0;
    ilen=TEST_LENGTH-olen;
    olen=TEST_LENGTH;
    if (iconv(ctoe,&test2,&ilen,&test3,&olen) == ((size_t)-1)) 
        return 0;
    if (iconv(ctoe,NULL,&ilen,&test3,&olen) == ((size_t)-1))
        return 0;

    if (strncmp(test1_0,test3_0,strlen(test1_0)) != 0)
        return 0;

    return 1;
}

static int
iconv_setup(current_code_name) 
     const char *current_code_name;
{
    iconv_t ctoe,etoc;
    static int disable_iconv = 0;

    if (disable_iconv) 
        return 0;
    if (eucjp_code_name == NULL) {
        if (! setup_eucjp_code_name()) {
	    disable_iconv = 1;
	    return 0;
	}
    }

    if (current_code_name == NULL || eucjp_code_name == NULL)
        return 0;

    ctoe = iconv_open(eucjp_code_name, current_code_name);
    etoc = iconv_open(current_code_name, eucjp_code_name);


    if (iconv_test(ctoe, etoc)) {
        /* ���ޤ����ä��� ���ꤹ�� */
        if (cur_to_euc != (iconv_t) -1) 
	    iconv_close(cur_to_euc);
	if (euc_to_cur != (iconv_t) -1) 
	    iconv_close(euc_to_cur);
	cur_to_euc=ctoe;
	euc_to_cur=etoc;
	return 1;
    } else {
        if (ctoe != (iconv_t)-1) 
	    iconv_close(ctoe);
	if (etoc != (iconv_t)-1) 
	    iconv_close(etoc);
	return 0;
    }
}
#endif

enum CONV_MODE detect_conv_mode(encoding) 
     const char *encoding;
{
    if (encoding == NULL) return IO_AUTO;
    if (match_str(encoding,euc_jp_names)) return IO_EUC;
    if (match_str(encoding,shift_jis_names)) return IO_SJIS;
#ifdef HAVE_ICONV
    if (match_str(encoding,iso_2022_jp_names)) {
        const char **enc;
	for(enc = iso_2022_jp_names;*enc != NULL; enc++){
	    if (iconv_setup(*enc))
	        return IO_ICONV;
	}
    } else if (iconv_setup(encoding)) {
        return IO_ICONV;
    }
#endif
    return IO_AUTO;
}

int
locale_init(encoding)
     const char *encoding;
{
    static int		initialized = 0;
#ifdef HAVE_SETLOCALE
    static char *locale_name = NULL;
    static char *current_code_name = NULL;
#endif
    enum CONV_MODE cm_temp;

#ifdef HAVE_SETLOCALE
    locale_name = setlocale(LC_CTYPE, "");
#endif

    /* 1. encoding �ˤ����� 
            �� ͭ���� encoding �����ꤵ���а������ͤ���  */
    cm_temp = detect_conv_mode(encoding);
    if (cm_temp != IO_AUTO) {
        conv_mode = cm_temp;
        goto init_finish;
    }

    /* ���Ǥ� ������ѤߤǤ���� ���Τޤ޵��� */
    if (initialized != 0 && 
	(conv_mode != IO_ICONV
#ifdef HAVE_ICONV
	 || (cur_to_euc != (iconv_t)-1 && euc_to_cur != (iconv_t)-1)
#endif
	 ))
	return CODECONV_OK;
    initialized = 0;
    conv_mode = IO_AUTO;

#ifdef HAVE_SETLOCALE
    /* 2. current_locale ���� ������ߤ� */
#if defined(HAVE_NL_LANGINFO) && defined(CODESET)
    /* 2a/2b. nl_langinfo(CODESET) ����μ����λ�� */
    current_code_name=nl_langinfo(CODESET);
    conv_mode=detect_conv_mode(current_code_name);
    if (conv_mode != IO_AUTO) 
        goto init_finish;
#endif
    /* 2c/2d. locale LC_CTYPE ���Τ�Τγ�ǧ */ 
    conv_mode=detect_conv_mode(locale_name);
    if (conv_mode != IO_AUTO) 
        goto init_finish;

    /* 2e/2f. locale LC_CTYPE ��Ⱦ���γ�ǧ */ 
    if (locale_name != NULL) {
        char *try2;
        locale_name = strdup(locale_name);
	if (locale_name == NULL) 
	    return CODECONV_ERROR;
	try2 = strtok(locale_name, ".@");
	if (try2 != NULL) 
	    try2 = strtok(NULL, ".@");
	if (try2 != NULL) {
	    conv_mode = detect_conv_mode(try2);
	    if (conv_mode != IO_AUTO) goto init_finish;
	}
    }
#endif /* HAVE_SETLOCALE */
    
    /* 3a/3b. ����Ǥ����ʤ� FALLBACK ���� */
#ifdef FALLBACK_ENCODING
    conv_mode = detect_conv_mode(FALLBACK_ENCODING);
#endif

    /* 4. �������� EUC_JP */
    if (conv_mode == IO_AUTO) conv_mode = IO_EUC;

 init_finish:
    initialized = 1;
    return CODECONV_OK;
}

static int
xputs_raw(str, len, fp)
     const char	*str;
     int	len;
     FILE	*fp;
{
    int outlen = 0;
    int len1 = len;
    int wlen;

    while (outlen < len) {
        wlen = fwrite(str, 1, len1, fp);
	if (wlen == 0) 
	    break;
	outlen += wlen;
	len1 -= wlen;
	str += wlen;
    }
    return outlen;
}

/* Convert ISO8859-1 NO-BREAK SPACE to normal SPACE */
/* NBSP(0xa0) �� EUC-JP �Ȥ֤Ĥ���ʤ�Ȧ */
static int
convert_nbsp(str, len)
     char *str;
     int len;
{
    while(len>0) {
      if (((*str) & 0xff) == 0xa0) *str = 0x20;
      str++;
      len--;
    }
    return 1;
}

static int
xputs2(str, len, fp)
     const char	*str;
     int	len;
     FILE	*fp;
{
	char	*buf1p, *buf1p0;
	char	*buf2p, *buf2p0;
	size_t	len1, len2;
	size_t	outlen;
	int		ret_code;
	size_t	status;

	/* The maximum size of output is 4 times larger than input. */
	outlen = len * 4;

	len1 = len;
	len2 = outlen;
	buf1p = buf1p0 = malloc(len1);
	if (buf1p == NULL)
		return EOF;
	buf2p = buf2p0 = malloc(len2);
	if (buf2p == NULL) {
		free(buf1p0);
		return EOF;
	}
	memcpy(buf1p, str, len);
	convert_nbsp(buf1p, len);
	status=euc_to_current(&buf1p, &len1, &buf2p, &len2);
	if (status == -2) { /* ������ �����ΰ����� */
		buf1p = buf1p0;
		len1 = len;
		outlen *= 3;
		len2 = outlen;
		free(buf2p0);
		buf2p = buf2p0 = malloc(outlen);
		if (buf2p == NULL){
			free(buf1p0);
			return EOF;
		}
		status=euc_to_current(&buf1p, &len1, &buf2p, &len2);
	} 
	if (status == CODECONV_ERROR || status == CODECONV_BUFFER_OVERFLOW) {
		/* Conversion Error  �������� ���Τޤ޽��� */
		free(buf1p0);
		free(buf2p0);
		return xputs_raw(str, len, fp);
	}
	free(buf1p0);
	ret_code = xputs_raw(buf2p0, outlen - len2, fp);
	free(buf2p0);
	return ret_code;
}

int
xfputs(str, fp)
     const char *str;
     FILE* fp;
{
    return xputs2(str, strlen(str), fp);
}

int
xputs(str)
     const char *str;
{
    int len;
    len=xfputs(str, stdout);
    if (len<0) return EOF;
    putchar('\n');
    return len+1;
}

int
xvfprintf(fp, fmt, ap)
    FILE *fp;
    const char *fmt;
    va_list ap;
{
    char buf1[TMP_SIZE];
    int len;
#ifdef HAVE_VSNPRINTF
    len = vsnprintf(buf1, TMP_SIZE - 1, fmt, ap);
    buf1[TMP_SIZE - 1]=0;
#else
    len = vsprintf(buf1, fmt, ap);
#endif
    return xputs2(buf1, len, fp);
}

/* USE_STDARG_H is defined in codeconv.h */
#ifdef USE_STDARG_H
int
xfprintf(FILE *fp, const char *fmt, ...)
#else
int
xfprintf(fp, fmt, va_alist)
    FILE	*fp;
    const char	*fmt;
    va_dcl
#endif
{
    int len;
    va_list ap;
#ifdef USE_STDARG_H
    va_start(ap, fmt);
#else
    va_start(ap);
#endif
    len = xvfprintf(fp, fmt, ap);
    va_end(ap);
    return len;
}

int
#ifdef USE_STDARG_H
xprintf(const char *fmt, ...)
#else
xprintf(fmt, va_alist)
    const char *fmt;
    va_dcl
#endif
{
    int len;
    va_list ap;
#ifdef USE_STDARG_H
    va_start(ap, fmt);
#else
    va_start(ap);
#endif
    len = xvfprintf(stdout, fmt, ap);
    va_end(ap);
    return len;
}

char *
xfgets(str, size, fp)
     char *str;
     int size;
     FILE *fp;
{
    char *ibuf, *ibuf0;
    size_t ilen;
    size_t status;
    char *str0;
    int size0;

    str0 = str;
    size0 = size;

    /* The maximum size of input is 4 times larger than size. */
    ilen = size * 4;
    ibuf0 = ibuf = malloc(ilen+1);
    if (ibuf == NULL) 
        return NULL;

    if (fgets(ibuf, ilen, fp) == NULL) {
        free(ibuf);
        return NULL;
    }
    ibuf[ilen]=0;
    ilen=strlen(ibuf);

    status = current_to_euc(&ibuf,&ilen,&str,(size_t *)&size);
    str0[size0-size]=0;
    free(ibuf0);
    if (status != CODECONV_ERROR) return str0;
    return NULL;
}

/* ================================================================== */
size_t current_to_euc (in_buf,in_len,out_buf,out_len)
     char **in_buf, **out_buf;
     size_t *in_len,*out_len;
{
    static int output_left = -1;
    int c1, c2;
    size_t count = 0;


#ifdef HAVE_ICONV
    if (conv_mode == IO_ICONV) {
        size_t ret;
        if (cur_to_euc == (iconv_t) -1)
	    return CODECONV_ERROR;
        ret = iconv(cur_to_euc,in_buf,in_len,out_buf,out_len);
	printf("HELLOBYE\n");
	fflush(stdout);
	if (ret != ((size_t)-1)) 
	    ret = iconv(cur_to_euc, NULL, in_len, out_buf, out_len);
#if defined (HAVE_ERRNO_H) && defined (E2BIG)
	if (ret == ((size_t)-1)) {
	    if (errno == E2BIG) 
	        return CODECONV_BUFFER_OVERFLOW;
	    return CODECONV_ERROR;
	}
#endif /* HAVE_ERRNO_H / E2BIG */
	return ret;
    }
#endif /* HAVE_ICONV */

    if (output_left >= 0) {
        if (*out_len > 0) {
	    *((*in_buf)++) = output_left;
	    (*out_len)--;
	    count++;
	    output_left = -1;
	} else {
  	    /* Output Buffer Overflow */
	    return CODECONV_BUFFER_OVERFLOW;
	}
    }
    if (conv_mode == IO_SJIS) {
        while(*in_len>0) {
	    if (*out_len<=0) break;
	    c1 = *((*in_buf)++) & 0xff;
	    (*in_len)--;
	    if (c1 < 0x80) { /* ASCII ʸ�� */
	        (*out_len)--;
		count++;
		*((*out_buf)++)=c1;
		continue;
	    } else if ((c1 < 0x81 || c1 > 0x9f) && (c1 < 0xe0 || c1 > 0xef)) {
		/*  Ⱦ�ѥ��� */
		if (0xa1 <= c1 && c1 <= 0xdf) {
		    c2 = c1 - 0x80;
		    c1 = 0x8e;
		} else {
		    return -1;
		}
	    } else {
		c2 = *((*in_buf)++) & 0xff;
		(*in_len)--;
		if (c1 > 0x9f)
		    c1 -= 0x40;
		c1 += c1;
		if (c2 <= 0x9e) {
		    c1 -= 0xe1;
		    if (c2 >= 0x80)
			c2 -= 1;
		    c2 -= 0x1f;
		} else {
		    c1 -= 0xe0;
		    c2 -= 0x7e;
		}
		c2 |= 0x80;
	    }
	    *((*out_buf)++) = c1 | 0x80;
	    (*out_len)--;
	    count++;
	    if (*out_len <= 0) {
	        output_left = c2;
		return CODECONV_BUFFER_OVERFLOW;
	    }
	    *((*out_buf)++) = c2;
	    (*out_len)--;
	    count++;
	}
	if (*in_len == 0) return count;
	if (*out_len == 0) return CODECONV_BUFFER_OVERFLOW;
	return CODECONV_ERROR;
    } else { /* IO_EUC */
        if (*out_len < *in_len) {
	    memcpy(*out_buf,*in_buf,*out_len);
	    count = *out_len;
	    (*out_buf) += *out_len;
	    (*in_buf) += *out_len;
	    (*in_len) -= *out_len;
	    *out_len = 0;
	    return CODECONV_BUFFER_OVERFLOW;
	} else {
	    memcpy(*out_buf,*in_buf,*in_len);
	    count = *in_len;
	    (*out_buf)+=*in_len;
	    (*in_buf)+=*in_len;
	    (*out_len)-=*in_len;
	    *in_len=0;
	    return count;
	}
    }
    return CODECONV_ERROR; /* Never */
}

size_t euc_to_current (in_buf,in_len,out_buf,out_len)
	char **in_buf, **out_buf;
	size_t *in_len,*out_len;
{
	static int output_left = -1;
	int c1, c2;
	size_t count = 0;

#ifdef HAVE_ICONV
	if (conv_mode == IO_ICONV) {
		size_t ret;
		if (euc_to_cur == (iconv_t) -1)
			return CODECONV_ERROR;
		ret = iconv(euc_to_cur,in_buf,in_len,out_buf,out_len);
		if (ret != ((size_t)-1))
			ret = iconv(euc_to_cur,NULL,in_len,out_buf,out_len);
#if defined (HAVE_ERRNO_H) && defined (E2BIG)
		if (ret == ((size_t)-1)) {
			if (errno == E2BIG) 
				return CODECONV_BUFFER_OVERFLOW;
			return CODECONV_ERROR;
		}
#endif /* HAVE_ERRNO_H / E2BIG */
		return ret;
	}
#endif /* HAVE_ICONV */

	if (output_left >= 0) {
		if (*out_len > 0) {
			*((*in_buf)++) = output_left;
			(*out_len)--;
			count++;
			output_left = -1;
		} else {
			/* Output Buffer Overflow */
			return CODECONV_BUFFER_OVERFLOW;
		}
	}
	if (conv_mode == IO_SJIS) {
		while(*in_len>0) {
			if (*out_len<=0) break;
			c1 = *((*in_buf)++) & 0xff;
			(*in_len)--;
			if ((c1 & 0x80) == 0) {
				*((*out_buf)++) = c1;
				(*out_len)--;
				count++;
				continue;
			}
			if (0x8e == c1) {
				*((*out_buf)++) = *((*in_buf)++) | 0x80;
				(*in_len)--;
				(*out_len)--;
				count++;
				continue;
			}
			c1 &= 0x7f;
			c2 = *((*in_buf)++) & 0x7f;
			(*in_len)--;
			if (c1 & 0x01) {
				c2 += 0x1f;
				if (c2 > 0x7e)
					c2++;
			} else {
				c2 += 0x7e;
			}
			c1 = (c1 + 0xe1) >> 1;
			if (c1 > 0x9f)
				c1 += 0x40;
			*((*out_buf)++) = c1;
			(*out_len)--;
			count++;
			if (*out_len <= 0) {
				output_left = c2;
				return CODECONV_BUFFER_OVERFLOW;
			}
			*((*out_buf)++) = c2;
			(*out_len)--;
			count++;
		}
		if (*in_len == 0) return count;
		if (*out_len == 0) return CODECONV_BUFFER_OVERFLOW;
		return CODECONV_ERROR;
	} else { /* IO_EUC */
		if (*out_len < *in_len) {
			memcpy(*out_buf,*in_buf,*out_len);
			count  = *out_len;
			(*out_buf)+=*out_len;
			(*in_buf)+=*out_len;
			(*in_len)-=*out_len;
			*out_len=0;
			return CODECONV_BUFFER_OVERFLOW;
		} else {
			memcpy(*out_buf,*in_buf,*in_len);
			count  = *in_len;
			(*out_buf)+=*in_len;
			(*in_buf)+=*in_len;
			(*out_len)-=*in_len;
			*in_len=0;
			return count;
		}
	}
	return CODECONV_ERROR; /* Never */
}

