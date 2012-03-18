
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <eb/eb.h>
#include <eb/text.h>
#include <eb/font.h>
#include <eb/appendix.h>
#include <eb/error.h>
#include <eb/binary.h>

#include "codeconv.h"


#define MAX_HIT_SIZE	256


EB_Book current_book;
EB_Appendix current_appendix;

EB_Hookset text_hookset, heading_hookset;

int last_search_begin = 0;

/*
EB_Hook text_hooks[] = {
  {EB_HOOK_NARROW_JISX0208, hook_euc_to_ascii},
  {EB_HOOK_NARROW_FONT,     hook_font},
  {EB_HOOK_WIDE_FONT,	    hook_font},
  {EB_HOOK_NEWLINE,         eb_hook_newline},
#ifdef EB_HOOK_STOP_CODE
  {EB_HOOK_STOP_CODE,       hook_stopcode},
#endif
  {EB_HOOK_BEGIN_MONO_GRAPHIC, hook_img},
  {EB_HOOK_END_MONO_GRAPHIC, hook_img},
  {EB_HOOK_BEGIN_COLOR_JPEG,hook_img},
  {EB_HOOK_BEGIN_COLOR_BMP, hook_img},
  {EB_HOOK_END_COLOR_GRAPHIC, hook_img},
#ifdef EB_HOOK_BEGIN_IN_COLOR_BMP
  {EB_HOOK_BEGIN_IN_COLOR_JPEG,hook_img},
  {EB_HOOK_BEGIN_IN_COLOR_BMP, hook_img},
  {EB_HOOK_END_IN_COLOR_GRAPHIC, hook_img},
#endif
//   {EB_HOOK_BEGIN_SOUND,     hook_tags},
//   {EB_HOOK_END_SOUND,       hook_tags},
  {EB_HOOK_BEGIN_REFERENCE, hook_tags},
  {EB_HOOK_END_REFERENCE,   hook_tags},
  {EB_HOOK_BEGIN_CANDIDATE, hook_tags},
  {EB_HOOK_END_CANDIDATE_GROUP, hook_tags},
#ifdef EB_HOOK_BEGIN_IMAGE_PAGE
  {EB_HOOK_BEGIN_IMAGE_PAGE, hook_img},
  {EB_HOOK_END_IMAGE_PAGE,   hook_img},
#endif
#ifdef EB_HOOK_BEGIN_GRAPHIC_REFERENCE
  {EB_HOOK_BEGIN_GRAPHIC_REFERENCE, hook_tags},
  {EB_HOOK_END_GRAPHIC_REFERENCE,   hook_tags},
  {EB_HOOK_GRAPHIC_REFERENCE, hook_tags},
#endif
#ifdef EB_HOOK_CLICKABLE_AREA
  {EB_HOOK_CLICKABLE_AREA, hook_tags},
#endif

  {EB_HOOK_BEGIN_SUBSCRIPT, hook_decoration},
  {EB_HOOK_END_SUBSCRIPT, hook_decoration},
  {EB_HOOK_BEGIN_SUPERSCRIPT, hook_decoration},
  {EB_HOOK_END_SUPERSCRIPT, hook_decoration},
  {EB_HOOK_BEGIN_NO_NEWLINE, hook_decoration},
  {EB_HOOK_END_NO_NEWLINE, hook_decoration},
  {EB_HOOK_BEGIN_EMPHASIS, hook_decoration},
  {EB_HOOK_END_EMPHASIS, hook_decoration},

  {EB_HOOK_NULL, NULL},
};
*/

int hitcomp(a, b)
     const void *a;
     const void *b;
{
  const EB_Hit *x, *y;
  x = (EB_Hit *)a;
  y = (EB_Hit *)b;
  if (x->heading.page < y->heading.page) return -1;
  if (x->heading.page == y->heading.page) {
    if (x->heading.offset < y->heading.offset) return -1;
    if (x->heading.offset == y->heading.offset) {
      if (x->text.page < y->text.page) return -1;
      if (x->text.page == y->text.page) {
	if (x->text.offset < y->text.offset) return -1;
	if (x->text.offset == y->text.offset) return 0;
      }
    }
  } 
  return 1;
}

int
search_pattern (book, appendix, pattern, begin, length)
     EB_Book *book;
     EB_Appendix *appendix;
     char *pattern;
     int begin;
     int length;
{
  int i, num, point;
  char headbuf1[BUFSIZ];
  char headbuf2[BUFSIZ];
  char *head;
  //const char *s;
  EB_Hit hitlist[MAX_HIT_SIZE];
  EB_Error_Code error_code = EB_SUCCESS;

  char* prevhead;
  int prevpage;
  int prevoffset;
  ssize_t heading_len;

  // search
  point = 0;
  error_code = eb_search_exactword(book, pattern);
  if (EB_SUCCESS != error_code) {
    xprintf ("An error occured in search_pattern: %s\n",
	    eb_error_message (error_code));
    exit(1);
  }

  head = headbuf1;
  prevhead = headbuf2;
  *prevhead = '\0';
  prevpage = 0;
  prevoffset = 0;

  while (EB_SUCCESS == eb_hit_list (book, MAX_HIT_SIZE, hitlist, &num)
	 && 0 < num) {
    qsort(hitlist, num, sizeof(EB_Hit), hitcomp);
    for (i = 0; i < num; i++) {
      point++;
      if (point >= begin + length && length > 0) {
	xprintf ("<more point=%d>\n", point);
	last_search_begin = point;
	goto exit;
      }

      if (point >= begin) {
  	error_code = eb_seek_text (book, &hitlist[i].heading);
        if (error_code != EB_SUCCESS)
	  continue;
	error_code = eb_read_heading (book, appendix, &heading_hookset, NULL,
				      BUFSIZ - 1, head, &heading_len);
        if (error_code != EB_SUCCESS || heading_len == 0)
	  continue;
        *(head + heading_len) = '\0';

	if (prevpage == hitlist[i].text.page &&
	    prevoffset == hitlist[i].text.offset &&
	    strcmp (head, prevhead) == 0)
	  continue;

	xprintf ("%2d. %d:%d\t", point,
	       hitlist[i].text.page, hitlist[i].text.offset);
	xfputs (head, stdout);
	fputc ('\n', stdout);
      }

      if (head == headbuf1) {
	head = headbuf2;
	prevhead = headbuf1;
      } else {
	head = headbuf1;
	prevhead = headbuf2;
      }
      prevpage = hitlist[i].text.page;
      prevoffset = hitlist[i].text.offset;
    }
  }
exit:
  return 1;
}

int parse_dict_id (char *name, EB_Book *book)
{
	int i, num;
	EB_Subbook_Code sublist[EB_MAX_SUBBOOKS];
	EB_Error_Code error_code = EB_SUCCESS;

	error_code = eb_subbook_list (book, sublist, &num);
	if (EB_SUCCESS != error_code)
		goto error;

	if ((i = atoi (name)) > 0) {
		/*
		 * Numbered dictionary
		 */
		if (--i < num) {
			error_code = eb_set_subbook (book, sublist[i]);
			if (EB_SUCCESS != error_code) {
				xprintf ("An error occurred in parse_dict_id: %s\n",
						eb_error_message (error_code));
				exit(1);
			}
			return 1;
		} else {
			xprintf ("No such numberd dictionary : %s\n", name);
			exit(1);
		}
	} else {
		/*
		 * Named dictionary
		 */
		char dir[PATH_MAX + 1];

		for (i = 0; i < num; i++) {
			error_code = eb_subbook_directory2 (book, sublist[i], dir);
			if (EB_SUCCESS != error_code) {
				xprintf ("An error occurred in parse_dict_id: %s\n",
						eb_error_message (error_code));
				exit(1);
			}

			if (strcmp (name, dir) == 0) {
				error_code = eb_set_subbook (book, sublist[i]);
				if (EB_SUCCESS != error_code) {
					xprintf ("An error occurred in parse_dict_id: %s\n",
							eb_error_message (error_code));
					exit(1);
				}
				return 1;
			}
		}
		xprintf ("No such dictionary: %s\n", name);
		exit(1);
	}

error:
	xprintf ("An error occurred in parse_dict_id: %s\n",
			eb_error_message (error_code));
	exit(1);
}

void list_subbooks()
{
	EB_Error_Code error_code = EB_SUCCESS;

	int i, j, num;
	char buff[EB_MAX_TITLE_LENGTH + 1];
	char buff2[EB_MAX_SUBBOOKS][PATH_MAX + 4];
	EB_Subbook_Code list[EB_MAX_SUBBOOKS];

	error_code = eb_subbook_list (&current_book, list, &num);
	if (EB_SUCCESS != error_code)
		goto error;

	for (i = 0; i < num; i++) {
		printf("%2d. ", i + 1);

		error_code = eb_subbook_directory2 (&current_book, list[i], buff2[i]);
		if (EB_SUCCESS != error_code)
			goto error;
		for(j = 0; j < i; j++) {
			if (strcmp(buff2[j], buff2[i]) == 0) {
				sprintf(buff, ".%d", i + 1);
				strcat(buff2[i], buff);
				break;
			}
		}

		xprintf("%s\t", buff2[i]);
		error_code = eb_subbook_title2 (&current_book, list[i], buff);
		if (EB_SUCCESS != error_code)
			goto error;

		xprintf("%s\n", buff);
	}

	return;

error:
	xprintf("An error occured in command_list: %s\n", eb_error_message (error_code));
	return;
}

int parse_entry_id (char *code, EB_Position *pos)
{
	EB_Error_Code error_code = EB_SUCCESS;

	if (strchr (code, ':') != NULL) {
		/*
		 * Encoded position
		 */
		char *endp;
		pos->page = strtol (code, &endp, 0);
		if (*endp != ':')
			goto illegal;

		pos->offset = strtol (endp + 1, &endp, 0);
		if (*endp != '\0')
			goto illegal;

		return 1;

illegal:
		xprintf ("Illegal position: %s\n", code);
		return 0;

	} else {
		/*
		 // Numbered entry
		int num, count;
		const char *pattern = variable_ref ("_last_search_pattern");
		EB_Hit list[MAX_HIT_SIZE];

		if (!pattern) {
			xputs ("No search has been executed yet.");
			return 0;
		}
		if ((count = atoi (code) - 1) < 0) {
			xprintf ("Invalid entry number: %s\n", code);
			return 0;
		}
		if (check_subbook ()) {
			error_code = last_search_function (&current_book, pattern);
			if (EB_SUCCESS != error_code) {
				xprintf ("An error occured in parse_entry_id: %s\n",
						eb_error_message (error_code));
				set_error_message (error_code);
				return 0;
			}
			while (EB_SUCCESS == eb_hit_list (&current_book, MAX_HIT_SIZE, list,
						&num) && 0 < num) {
				qsort(list, num, sizeof(EB_Hit), hitcomp);
				if (count < num) {
					pos->page = list[count].text.page;
					pos->offset = list[count].text.offset;
					return 1;
				}
				count -= num;
				pattern = NULL;
			}
			if (num == 0)
				xprintf ("Too big: %s\n", code);
		}
		return 0;
		*/
		// I DON'T WANT TO CODE THIS YET
		printf("ERROR!\n");
		exit(1);
	}
}

int
insert_content (book, appendix, pos, begin, length)
     EB_Book *book;
     EB_Appendix *appendix;
     EB_Position *pos;
     int begin;
     int length;
{
  int point;
  ssize_t len;
  char last = '\n';
  char buff[EB_SIZE_PAGE];
  EB_Error_Code error_code = EB_SUCCESS;
  FILE *outFP = stdout;

  /* insert */
  point = 0;
  error_code = eb_seek_text(book, pos);
  if (error_code != EB_SUCCESS) {
    xprintf("An error occured in seek_position: %s\n",
	   eb_error_message(error_code));
	exit(1);
  }

  while (EB_SUCCESS == eb_read_text (book, appendix, &text_hookset, NULL,
				     EB_SIZE_PAGE - 1, buff, &len) &&
	 0 < len) {
    *(buff + len) = '\0';
    /* count up */
    point++;
    if (point >= begin + length && length > 0) {
      xfprintf (outFP, "<more point=%d>\n", point);
      goto exit;
    }

    /* insert */
    if (point >= begin) {
      xfputs (buff, outFP);
      last = buff[len - 1];
    }
  }

  /* insert a newline securely */
  if (last != '\n')
    putc('\n', outFP);

  //insert_prev_next(book, appendix, pos, outFP);

 exit:
  return 1;
}

int
main (argc, argv)
     int argc;
     char *const *argv;
{
  //int optch;
  //int no_init = 0;
  //char buff[BUFSIZ];
  const char *book, *appendix/*, *s*/;
  //FILE *fp;
  EB_Character_Code charcode;
  EB_Error_Code error_code = EB_SUCCESS;

  locale_init(NULL);

  // check the rest arguments
  book = appendix = NULL;
  book = "/home/illabout/temp/jp-dicts/JJ - Daijirin";

  // initialize variables
  eb_initialize_library ();
  eb_initialize_book (&current_book);
  eb_initialize_appendix (&current_appendix);
  eb_initialize_hookset (&text_hookset);
  eb_initialize_hookset (&heading_hookset);
  //eb_set_hooks (&text_hookset, text_hooks);
  //eb_set_hooks (&heading_hookset, heading_hooks);

  // set book and appendix
  if (book) {
	  error_code = eb_bind (&current_book, book);
	  if (EB_SUCCESS != error_code) {
		  xprintf("Warning: invalid book directory: %s (%s)\n", book,
				  eb_error_message(error_code));
		  exit(1);
	  }
  }
  if (appendix) {
	  error_code = eb_bind_appendix (&current_appendix, appendix);
	  if (EB_SUCCESS != error_code) {
		  xprintf ("Warning: invalid appendix directory: %s (%s)\n", appendix,
				  eb_error_message(error_code));
		  exit(1);
	  }
  }

  // check the book directory
  if (!eb_is_bound (&current_book)) {
	  xprintf("Warning: you should specify a book directory first\n");
	  exit(1);
  }

  // kanji code
  error_code = eb_character_code (&current_book, &charcode);
  if (EB_SUCCESS != error_code) {
	  xprintf ("Warning: invalid character code: (%s)\n", eb_error_message(error_code));
	  exit(1);
  }

  // list all the available subbooks
  list_subbooks();

  // select the subbook
  if (parse_dict_id ("1", &current_book)) {
	  if (eb_is_appendix_bound (&current_appendix))
	  {
		  EB_Subbook_Code code;

		  eb_subbook (&current_book, &code);
		  eb_set_appendix_subbook (&current_appendix, code);
	  }
  }

  char *ibuf;
  size_t ilen = strlen(argv[2]);
  ibuf = malloc(ilen + 1);
  memcpy(ibuf, argv[2], strlen(argv[2]));
  ibuf[ilen] = '\0';
  printf("blahalahalah = aaa%saaa length = %ld, ilen = %ld, argv2len = %ld\n",
		  ibuf, strlen(ibuf), ilen, strlen(argv[2]));

  char *obuf;
  size_t olen = ilen;
  obuf = calloc(1, olen + 1);

  char *other;
  size_t other_len;
  other = obuf;
  other_len = olen;

  size_t status;

  status = current_to_euc(&ibuf, &ilen, &obuf, &olen);
  other[other_len - olen] = '\0';
  if (status == CODECONV_ERROR) {
	  xprintf("Error with converting argv[2] to euc\n");
  }

  xprintf("other: %s\n", other);


  printf("first arg: %s\n", argv[2]);
  search_pattern (&current_book, &current_appendix, other, 0, MAX_HIT_SIZE);

  EB_Position pos;
  parse_entry_id("156819:334", &pos);
  insert_content(&current_book, &current_appendix, &pos, 1, 0);

  eb_finalize_library ();
  return 0;
}

