
/* With z88dk, library editing may not work on some target    */
/* '-DNOEDIT' will limit the tool (and its size) in those cases            */

/* zcc +cpm -create-app -O3 --opt-code-size -DTOUPPER lar.c */
/* zcc +cpm -create-app -O3 --opt-code-size -DTOUPPER -DNOEDIT lar.c */

/* Auto-expansion of compressed files, Stefano Bodrato, Oct - 2019 */
/* '-DUSQ' will add the automatic UNSQUEEZing of the file when appropriate */
/* '-DUNCRUNCH' will add the automatic UNCRUNCHing of the file when appropriate */
/* zcc +cpm -create-app -O3 --opt-code-size -DTOUPPER -DUSQ -DUNCRUNCH lar.c */


/*% /bin/env - /bin/ncc -O lar.c -o lar
From linus!sch Tue Jul 26 08:07:37 1983
Subject: CP/M Lu library maintainer

When transfering files to my personal computer, I often want to transfer
several files at once using the Umodem program.  To do this I wrote
the following small program to combine files for the CP/M LU program.

No special treatment necessary, just:
	cc -O lar.c -o lar
to make it.

-- 
Stephen Hemminger,  Mitre Corp. Bedford MA 
	{allegra,genrad,ihnp4, utzoo}!linus!sch	(UUCP)
	linus!sch@mitre-bedford			(ARPA)
----------------- lar.c ----------------------
*/

/*
 * Lar - LU format library file maintainer
 * by Stephen C. Hemminger
 *	linus!sch	or	sch@Mitre-Bedford
 *
 *  Usage: lar key library [files] ...
 *
 *  Key functions are:
 *	u - Update, add files to library
 *	t - Table of contents
 *	e - Extract files from library
 *	p - Print files in library
 *	d - Delete files in library
 *	r - Reorganize library
 *  Other keys:
 *	v - Verbose
 *
 *  This program is public domain software, no warranty intended or
 *  implied.
 *
 *  DESCRPTION
 *     Lar is a Unix program to manipulate CP/M LU format libraries.
 *     The original CP/M library program LU is the product
 *     of Gary P. Novosielski. The primary use of lar is to combine several
 *     files together for upload/download to a personal computer.
 *
 *  PORTABILITY
 *     The code is modeled after the Software tools archive program,
 *     and is setup for Version 7 Unix.  It does not make any assumptions
 *     about byte ordering, explict and's and shift's are used.
 *     If you have a dumber C compiler, you may have to recode new features
 *     like structure assignment, typedef's and enumerated types.
 *
 *  BUGS/MISFEATURES
 *     The biggest problem is text files, the programs tries to detect
 *     text files vs. binaries by checking for non-Ascii (8th bit set) chars.
 *     If the file is text then it will throw away Control-Z chars which
 *     CP/M puts on the end.  All files in library are padded with Control-Z
 *     at the end to the CP/M sector size if necessary.
 *
 *     No effort is made to handle the difference between CP/M and Unix
 *     end of line chars.  CP/M uses Cr/Lf and Unix just uses Lf.
 *     The solution is just to use the Unix command sed when necessary.
 *
 *  * Unix is a trademark of Bell Labs.
 *  ** CP/M is a trademark of Digital Research.
 */



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>



/*  USQ function extracted from source version 3.2, 23/03/2012
	..in turn derived from the historical version 3.1 (12/19/84)  */

#ifdef USQ

/* z88dk specific optimizations */
#ifdef Z80
int getw16(FILE *iob) __z88dk_fastcall;
int getx16(FILE *iob) __z88dk_fastcall;
int getuhuff(FILE *ib) __z88dk_fastcall;
int getcr(FILE *ib) __z88dk_fastcall;
void unsqueeze(char *infile) __z88dk_fastcall;
#endif


#define SPEOF 256	/* special endfile token */
#define NUMVALS 257	/* 256 data values plus SPEOF*/

#define ERROR -1
#define DLE 0x90
#define RECOGNIZE 0xFF76	/* unlikely pattern */

#define LARGE 30000

unsigned int crc;	/* error check code */


/* Decoding tree */
struct {
	int children[2];	/* left, right */
} dnode[NUMVALS - 1];

int bpos;	/* last bit position read */
int curin;	/* last byte value read */

/* Variables associated with repetition decoding */
int repct;	/*Number of times to retirn value*/
int value;	/*current byte value or EOF */

/* This must follow all include files */
unsigned int dispcnt;	/* How much of each file to preview */
char	ffflag;		/* should formfeed separate preview from different files */


/* get 16-bit word from file */
int getw16(FILE *iob)
{
int temp;

temp = getc(iob);		/* get low order byte */
temp |= getc(iob) << 8;
if (temp & 0x8000) temp |= (~0) << 15;	/* propogate sign for big ints */
return temp;

}

/* get 16-bit (unsigned) word from file */
int getx16(FILE *iob)
{
int temp;

temp = getc(iob);		/* get low order byte */
return temp | (getc(iob) << 8);

}


/* initialize decoding functions */

void init_cr()
{
	repct = 0;
}

void init_huff()
{
	bpos = 99;	/* force initial read */
}


/* Decode file stream into a byte level code with only
 * repetition encoding remaining.
 */

int getuhuff(FILE *ib)
{
	int i;

	/* Follow bit stream in tree to a leaf*/
	i = 0;	/* Start at root of tree */
	do {
		if(++bpos > 7) {
			if((curin = getc(ib)) == ERROR)
				return ERROR;
			bpos = 0;
			/* move a level deeper in tree */
			i = dnode[i].children[1 & curin];
		} else
			i = dnode[i].children[1 & (curin >>= 1)];
	} while(i >= 0);

	/* Decode fake node index to original data value */
	i = -(i + 1);
	/* Decode special endfile token to normal EOF */
	i = (i == SPEOF) ? EOF : i;
	return i;
}

/* Get bytes with decoding - this decodes repetition,
 * calls getuhuff to decode file stream into byte
 * level code with only repetition encoding.
 *
 * The code is simple passing through of bytes except
 * that DLE is encoded as DLE-zero and other values
 * repeated more than twice are encoded as value-DLE-count.
 */

int getcr(FILE *ib)
{
	int c;

	if(repct > 0) {
		/* Expanding a repeated char */
		--repct;
		return value;
	} else {
		/* Nothing unusual */
		if((c = getuhuff(ib)) != DLE) {
			/* It's not the special delimiter */
			value = c;
			if(value == EOF)
				repct = LARGE;
			return value;
		} else {
			/* Special token */
			if((repct = getuhuff(ib)) == 0)
				/* DLE, zero represents DLE */
				return DLE;
			else {
				/* Begin expanding repetition */
				repct -= 2;	/* 2nd time */
				return value;
			}
		}
	}
}



void unsqueeze(char *infile)
{
	FILE *inbuff, *outbuff;	/* file buffers */
	int i, c;
	char cc;

	char *p;
	unsigned int filecrc;	/* checksum */
	int numnodes;		/* size of decoding tree */
	char outfile[128];	/* output file name */
	unsigned int linect;	/* count of number of lines previewed */
	char obuf[128];		/* output buffer */
	int oblen;		/* length of output buffer */
	static char errmsg[] = "ERROR - write failure in %s\n";

	if(!(inbuff=fopen(infile, "rb"))) {
		printf("Can't open %s\n", infile);
		return;
	}
	/* Initialization */
	linect = 0;
	crc = 0;
	init_cr();
	init_huff();

	/* Process header */
	if(getx16(inbuff) != RECOGNIZE) {
		//printf(" is not a squeezed file\n");
		goto closein;
	}

	filecrc = getw16(inbuff);

	/* Get original file name */
	p = outfile;			/* send it to array */
	do {
		*p = getc(inbuff);
	} while(*p++ != '\0');

	printf("-> %s: ", outfile);


	numnodes = getw16(inbuff);

	if(numnodes < 0 || numnodes >= NUMVALS) {
		printf("%s has invalid decode tree size\n", infile);
		goto closein;
	}

	/* Initialize for possible empty tree (SPEOF only) */
	dnode[0].children[0] = -(SPEOF + 1);
	dnode[0].children[1] = -(SPEOF + 1);

	/* Get decoding tree from file */
	for(i = 0; i < numnodes; ++i) {
		dnode[i].children[0] = getw16(inbuff);
		dnode[i].children[1] = getw16(inbuff);
	}

	if(dispcnt) {
		/* Use standard output for previewing */
		putchar('\n');
		while(((c = getcr(inbuff)) != EOF) && (linect < dispcnt)) {
			cc = 0x7f & c;	/* strip parity */
			if((cc < ' ') || (cc > '~'))
				/* Unprintable */
				switch(cc) {
				case '\r':	/* return */
					/* newline will generate CR-LF */
					goto next;
				case '\n':	/* newline */
					++linect;
				case '\f':	/* formfeed */
				case '\t':	/* tab */
					break;
				default:
					cc = '.';
				}
			putchar(cc);
		next: ;
		}
		if(ffflag)
			putchar('\f');	/* formfeed */
	} else {
		/* Create output file */
		if(!(outbuff=fopen(outfile, "wb"))) {
			printf("Can't create %s\n", outfile);
			goto closeall;
		}
		printf("unsqueezing,");
		/* Get translated output bytes and write file */
		oblen = 0;
		while((c = getcr(inbuff)) != EOF) {
			crc += c;
			obuf[oblen++] = c;
			if (oblen >= sizeof(obuf)) {
				if(!fwrite(obuf, sizeof(obuf), 1, outbuff)) {
					printf(errmsg, outfile);
					goto closeall;
				}
				oblen = 0;
			}
		}
		if (oblen && !fwrite(obuf, oblen, 1, outbuff)) {
			printf(errmsg, outfile);
			goto closeall;
		}

		if((filecrc && 0xFFFF) != (crc && 0xFFFF))
			printf("ERROR - checksum error in %s\n", outfile);
		else	printf(" done.\n");

	closeall:
		fclose(outbuff);
	}

closein:
	fclose(inbuff);
}


#endif  // USQ




#ifdef UNCRUNCH

/*Macro definition - ensure letter is lower case*/
//#define tolower(c) (((c)>='A' && (c)<='Z')?(c)-('A'-'a'):(c))

#define TABLE_SIZE  4096	/*size of main lzw table for 12 bit codes*/
#define XLATBL_SIZE 5003	/*size of physical translation table*/

/*special values for predecessor in table*/
#define NOPRED 0x6fff		/*no predecessor in table*/
#define EMPTY  0x8000		/*empty table entry (xlatbl only)*/
#define REFERENCED 0x2000	/*table entry referenced if this bit set*/
#define IMPRED 0x7fff		/*impossible predecessor*/

#define EOFCOD 0x100		/*special code for end-of-file*/
#define RSTCOD 0x101		/*special code for adaptive reset*/
#define NULCOD 0x102		/*special filler code*/
#define SPRCOD 0x103		/*spare special code*/

#define REPEAT_CHARACTER 0x90	/*following byte is repeat count*/

#ifdef	DUMBLINKER

  /*main lzw table and it's structure*/
  struct entry {
	short predecessor;	/*index to previous entry, if any*/
	unsigned char suffix;		/*character suffixed to previous entries*/
  } *lzw_table;

  /*auxilliary physical translation table*/
  /*translates hash to main table index*/
  short *xlatbl;

  /*byte string stack used by decode*/
  unsigned char *stack;

#else

  struct entry {
	short predecessor;	/*index to previous entry, if any*/
	unsigned char suffix;	/*character suffixed to previous entries*/
  } lzw_table[TABLE_SIZE];

  /*auxilliary physical translation table*/
  /*translates hash to main table index*/
  short xlatbl[XLATBL_SIZE];

  /*byte string stack used by decode*/
  unsigned char stack[TABLE_SIZE];

#endif

/*other global variables*/
unsigned char	codlen;		/*variable code length in bits (9-12)*/
short	trgmsk;			/*mask for codes of current length*/
unsigned char	fulflg;		/*full flag - set once main table is full*/
short	entry;			/*next available main table entry*/
long	getbuf;			/*buffer used by getcode*/
short	getbit;			/*residual bit counter used by getcode*/
unsigned char	entflg; 	/*inhibit main loop from entering this code*/
unsigned char	repeat_flag;	/*so send can remember if repeat required*/
int	finchar;		/*first character of last substring output*/
int	lastpr;			/*last predecessor (in main loop)*/
short	cksum;			/*checksum of all bytes written to output file*/

FILE 	*infd;			/*currently open input file*/
FILE 	*outfd;			/*currently open output file*/




/*find an empty entry in xlatbl which hashes from this predecessor/suffix*/
/*combo, and store the index of the next available lzw table entry in it*/
figure(pred,suff)
int pred;
int suff;
	{
	short *hash();
	auto int disp;
	register short *p;
	p=hash(pred,suff,&disp);

	/*follow secondary hash chain as necessary to find an empty slot*/
	while(((*p)&0xffff) != EMPTY)
		{
		p+=disp;
		if(p<xlatbl || p > xlatbl+XLATBL_SIZE)
			p+=XLATBL_SIZE;
		}

	/*stuff next available index into this slot*/
	*p=entry;
	}



/*enter the next code into the lzw table*/
enterx(pred,suff)
int pred;		/*table index of predecessor*/
int suff;		/*suffix byte represented by this entry*/
	{
	register struct entry *ep;
	ep = &lzw_table[entry];


	/*update xlatbl to point to this entry*/
	figure(pred,suff);

	/*make the new entry*/
	ep->predecessor = (short)pred;
	ep->suffix = (unsigned char)suff;
	entry++;

	/*if only one entry of the current code length remains, update to*/
	/*next code length because main loop is reading one code ahead*/
	if(entry >= trgmsk)
		{
		if(codlen<12)
			{
			/*table not full, just make length one more bit*/
			codlen++;
			trgmsk=(trgmsk<<1)|1;
			}
		else
			{
			/*table almost full (fulflg==0) or full (fulflg==1)*/
			/*just increment fulflg - when it gets to 2 we will*/
			/*never be called again*/
			fulflg++;
			}
		}
	}



/*initialize the lzw and physical translation tables*/
initb2()
	{
	register int i;
	register struct entry *p;
	p=lzw_table;

	/*first mark all entries of xlatbl as empty*/
	for(i=0;i<XLATBL_SIZE;i++) xlatbl[i]=EMPTY;

	/*enter atomic and reserved codes into lzw table*/
	for(i=0;i<0x100;i++) enterx(NOPRED,i);	/*first 256 atomic codes*/
	for(i=0;i<4;i++) enterx(IMPRED,0);	/*reserved codes*/
	}
	




/*hash pred/suff into xlatbl pointer*/
/*duplicates the hash algorithm used by CRUNCH 2.3*/
short *hash(pred,suff,disploc)
int pred;
int suff;
int *disploc;
	{
	register int hashval;
	
	hashval=((((pred>>4) & 0xff) ^suff) | ((pred&0xf)<<8)) + 1;
	*disploc=hashval-XLATBL_SIZE;
	return (xlatbl + hashval);
	}
	

/*initialize variables for each file to be uncrunched*/
intram()
	{
	trgmsk=0x1ff;	/*nine bits*/
	codlen=9;	/*    "    */
	fulflg=0;	/*table empty*/
	entry=0;	/*    "      */
	getbit=0;	/*buffer emtpy*/
	entflg=1;	/*first code always atomic*/
	repeat_flag=0;	/*repeat not active*/
	cksum=0;	/*zero checsum*/
	}


/*return a code of length "codlen" bits from the input file bit-stream*/
getcode()
	{
	register int hole;
	int code;

	/*always get at least a byte*/
	getbuf=(getbuf<<codlen)|(((long)getc(infd))<<(hole=codlen-getbit));
	getbit=8-hole;

	/*if is not enough to supply codlen bits, get another byte*/
	if(getbit<0)
		{
		getbuf |= ((long)getc(infd))<<(hole-8);
		getbit+=8;
		}

	if(feof(infd))
		{
		printf("***** Unexpected EOF on input file!\n");
		return EOFCOD;
		}

	/*skip spare or null codes*/
	if((code=((getbuf>>8) & trgmsk)) == NULCOD || code == SPRCOD)
		{
		return getcode();	/*skip this code, get next*/
		}

	return code;
	}


/*write a byte to output file*/
/*repeat byte (0x90) expanded here*/
/*checksumming of output stream done here*/
send(c)
register unsigned char c;
	{
	static unsigned char savec;	/*previous byte put to output*/

	/*repeat flag may have been set by previous call*/
	if(repeat_flag)
		{

		/*repeat flag was set - emit (c-1) copies of savec*/
		/*or (if c is zero), emit the repeat byte itself*/
		repeat_flag=0;
		if(c)
			{
			cksum+=(savec&0xff)*(c-1);
			while(--c){
				putc(savec,outfd);
			}
			}
		else
			{
			putc(REPEAT_CHARACTER,outfd);
			cksum+=REPEAT_CHARACTER;
			}
		}
	else
		{
		/*normal case - emit c or set repeat flag*/
		if(c==REPEAT_CHARACTER)
			{
			repeat_flag++;
			}
		else
			{
			putc(savec=c,outfd);
			cksum+=(c&0xff);
			}
		}
	}
	

/*decode this code*/
decode(code)
short code;
	{
	register unsigned char *stackp;		/*byte string stack pointer*/
	register struct entry *ep;
	ep = &lzw_table[code];

	if(code>=entry)
		{
		/*the ugly exception, "WsWsW"*/
		entflg=1;
		enterx(lastpr,finchar);
		}

	/*mark corresponding table entry as referenced*/
	ep->predecessor |= REFERENCED;

	/*walk back the lzw table starting with this code*/
	stackp=stack;
	while(ep > &lzw_table[255]) /*i.e. code not atomic*/
		{
		*stackp++ = ep->suffix;
		ep = &lzw_table[(ep->predecessor)&0xfff];
		}

	/*then emit all bytes corresponding to this code in forward order*/
	send(finchar=(ep->suffix)&0xff); /*first byte*/

	while(stackp > stack)		 /*the rest*/
		{
		send(*--stackp);
		}

	return(entflg);
	}





/*attempt to reassign an existing code which has*/
/*been defined, but never referenced*/
entfil(pred,suff)
int pred;		/*table index of predecessor*/
int suff;		/*suffix byte represented by this entry*/
	{
	auto int disp;
	register struct entry *ep;
	short *hash();
	short *p;
	p=hash(pred,suff,&disp);

	/*search the candidate codes (all those which hash from this new*/
	/*predecessor and suffix) for an unreferenced one*/
	while(*p!=(short)EMPTY){

		/*candidate code*/
		ep = &lzw_table[*p];
		if(((ep->predecessor)&REFERENCED)==0){
			/*entry reassignable, so do it!*/
			ep->predecessor=pred;
			ep->suffix=suff;
			/*discontinue search*/
			break;
		}

		/*candidate unsuitable - follow secondary hash chain*/
		/*and keep searching*/
		p+=disp;
		if(p<xlatbl || p > xlatbl+XLATBL_SIZE)
			p+=XLATBL_SIZE;
	}
}

/*
 cisubstr(string, token) searches for lower case token in string s
 returns pointer to token within string if found, NULL otherwise
*/

#define cisubstr(s,t) strstr(s,t)



/*uncrunch a single file*/
uncrunch(filename)
char *filename;
	{
	int c;			
	unsigned char *p;
	//char *cisubstr();
	unsigned char outfn[80];	/*space to build output file name*/
	int pred;			/*current predecessor (in main loop)*/
	unsigned char reflevel;		/*ref rev level from input file*/
	unsigned char siglevel;		/*sig rev level from input file*/
	unsigned char errdetect;	/*error detection flag from input file*/
	short file_cksum;		/*checksum read from input file*/

	/*initialize variables for uncrunching a file*/
	intram();

	/*open input file*/
	if ( 0 == (infd = fopen(filename,"rb")) )
		{
		printf("***** can't open %s\n", filename);
		return;
		}

	/*verify this is a crunched file*/
	if (getc(infd) != 0x76 || getc(infd) != 0xfe)
		{
		//printf("***** %s is not a crunched file\n",filename);
		return;
		}

	/*extract and build output file name*/
	printf("%s --> ",filename);
	for(p=outfn; (*p=getc(infd))!='\0'; p++) *p=tolower(*p);
	*(cisubstr(outfn,".")+4)='\0'; /*truncate non-name portion*/
	printf("%s\n",outfn);

	/*open output file*/
	if ( 0 == (outfd =fopen( outfn,"wb")) )
		{
		printf("***** can't create %s\n",outfn);
		return;
		}

	/*read the four info bytes*/
	reflevel=getc(infd);
	siglevel=getc(infd);
	errdetect=getc(infd);
	getc(infd); /*skip spare*/

	/*make sure we can uncrunch this format file*/
	/*note: this program does not support CRUNCH 1.x format*/
	if(siglevel < 0x20 || siglevel > 0x2f)
		{
		printf("***** this version of UNCR cannot process %s!\n",
			filename);
		return;
		}

	/*set up atomic code definitions*/
	initb2();

	/*main decoding loop*/
	pred=NOPRED;
	for(;;)
		{
		/*remember last predecessor*/
		lastpr=pred;

		/*read and process one code*/
		if((pred=getcode())==EOFCOD) /*end-of-file code*/
			{
			break; /*all lzw codes read*/
			}

		else if(pred==RSTCOD) /*reset code*/
			{
			entry=0;
			fulflg=0;
			codlen=9;
			trgmsk=0x1ff;
			pred=NOPRED;
			entflg=1;
			initb2();
			}

		else /*a normal code (nulls already deleted)*/
			{
			/*check for table full*/
			if(fulflg!=2)
				{
				/*strategy if table not full*/
				if(decode(pred)==0)enterx(lastpr,finchar);
				else entflg=0;
				}
			else
				{
				/*strategy if table is full*/
				decode(pred);
				entfil(lastpr,finchar); /*attempt to reassign*/
				}
			}
		}

	/*verify checksum if required*/
	if(errdetect==0)
		{
		file_cksum=getc(infd);
		file_cksum|=getc(infd)<<8;
		if(file_cksum!=cksum)
			{
			printf("***** checksum error detected in ");
			printf("%s!\n",filename);
			}
		}

	/*close files*/
	fclose(infd);
	fclose(outfd);

	/*all done this file*/
	return;
	}
	
#endif  // UNCRUNCH




#define ACTIVE	00
#define UNUSED	0xff
#define DELETED 0xfe
#define CTRLZ	0x1a

#define MAXFILES 256
#define SECTOR	 128
#define DSIZE	( sizeof(struct ludir) )
#define SLOTS_SEC (SECTOR/DSIZE)
#define equal(s1, s2) ( strcmp(s1,s2) == 0 )
/* if you don't have void type just define as blank */
#define VOID

/* if no enum's then define false as 0 and true as 1 and bool as int */
typedef enum {false=0, true=1} bool;

/* Globals */
char   *fname[MAXFILES];
bool ftouched[MAXFILES];

typedef struct {
    unsigned char   lobyte;
    unsigned char   hibyte;
} word;


/* convert word to int */

//#define wtoi(w) ( (w.hibyte<<8) + w.lobyte)
int wtoi(word w) {
	return ((w.hibyte<<8) + w.lobyte);
};

//#define itow(dst,src)	dst.hibyte = (src & 0xff00) >> 8; dst.lobyte = src & 0xff;
void itow(word dst,int src) {
	dst.hibyte = (src & 0xff00) >> 8;
	dst.lobyte = src & 0xff;
};


struct ludir {			/* Internal library ldir structure */
    unsigned char   l_stat;	/*  status of file */
    char    l_name[8];		/*  name */
    char    l_ext[3];		/*  extension */
    word    l_off;		/*  offset in library */
    word    l_len;		/*  lengty of file */
    char    l_fill[16];		/*  pad to 32 bytes */
} ldir[MAXFILES];


int     errcnt, nfiles, nslots;
bool	verbose = false;
char	*cmdname;

/* z88dk specific optimizations */
#ifdef Z80
int error (char *str) __z88dk_fastcall;
int cant (char *name) __z88dk_fastcall;
int getdir (FILE *f) __z88dk_fastcall;
int filarg (char *name) __z88dk_fastcall;
#endif

//char   *getfname(), *sprintf();
int	table(), extract(), print();

/* print error message and exit */
help () {
    fprintf (stderr, "Usage: %s {utepdr}[v] library [files] ...\n", cmdname);
    fprintf (stderr, "Functions are:\n");
#ifndef NOEDIT
	fprintf (stderr, "\tu - Update, add files to library\n");
#endif
    fprintf (stderr, "\tt - Table of contents\n");
    fprintf (stderr, "\te - Extract files from library\n");
    fprintf (stderr, "\tp - Print files in library\n");
#ifndef NOEDIT
    fprintf (stderr, "\td - Delete files in library\n");
    fprintf (stderr, "\tr - Reorginize library\n");
#endif
    fprintf (stderr, "Flags are:\n\tv - Verbose\n");
    exit (1);
}


conflict() {
   fprintf(stderr,"Conficting keys\n");
   help();
}


error (str)
char   *str;
{
    fprintf (stderr, "%s: %s\n", cmdname, str);
    exit (1);
}


cant (name)
char   *name;
{
    //extern int  errno;
    //extern char *sys_errlist[];

    fprintf (stderr, "Cannot open file :%s\n", name);
    exit (1);
}


/* Get file names, check for dups, and initialize */
filenames (ac, av)
int   ac;
char  **av;
{
    register int    i, j;

    errcnt = 0;
    for (i = 0; i < ac - 3; i++) {
	fname[i] = av[i + 3];
	ftouched[i] = false;
	if (i == MAXFILES)
	    error ("Too many file names.");
    }
    fname[i] = NULL;
    nfiles = i;
    for (i = 0; i < nfiles; i++)
	for (j = i + 1; j < nfiles; j++)
	    if (equal (fname[i], fname[j])) {
		fprintf (stderr, "%s", fname[i]);
		error (": duplicate file name");
	    }
}


getdir (f)
FILE *f;
{

    rewind(f);

    if (fread ((char *) & ldir[0], DSIZE, 1, f) != 1)
	error ("No directory\n");

    nslots = wtoi (ldir[0].l_len) * SLOTS_SEC;

    if (fread ((char *) & ldir[1], DSIZE, nslots-1, f) != nslots-1)
	error ("Can't read directory - is it a library?");
}


/* filarg - check if name matches argument list */
filarg (name)
char   *name;
{
    register int    i;

    if (nfiles <= 0)
	return 1;

    for (i = 0; i < nfiles; i++)
	if (equal (name, fname[i])) {
	    ftouched[i] = true;
	    return 1;
	}

    return 0;
}


not_found () {
    register int    i;

    for (i = 0; i < nfiles; i++)
	if (!ftouched[i]) {
	    fprintf (stderr, "%s: not in library.\n", fname[i]);
	    errcnt++;
	}
}


/* convert nm.ex to a Unix style string */
char   *getfname (nm, ex)
char   *nm, *ex;
{
    static char namebuf[14];
    register char  *cp, *dp;

    for (cp = namebuf, dp = nm; *dp != ' ' && dp != &nm[8];) {
#ifdef TOUPPER
	*cp++ = islower (*dp) ? toupper (*dp) : *dp;
#else
	*cp++ = isupper (*dp) ? tolower (*dp) : *dp;
#endif
	++dp;
    }
    *cp++ = '.';

    for (dp = ex; *dp != ' ' && dp != &ex[3];) {
#ifdef TOUPPER
	*cp++ = islower (*dp) ? toupper (*dp) : *dp;
#else
	*cp++ = isupper (*dp) ? tolower (*dp) : *dp;
#endif
	++dp;
    }

    *cp = '\0';
    return namebuf;
}


table (lib)
char   *lib;
{
    FILE   *lfd;
    register int    i, total;
    int active = 0, unused = 0, deleted = 0;
    char *uname;

    if ((lfd = fopen (lib, "rb")) == NULL)
	cant (lib);

    getdir (lfd);
    total = wtoi(ldir[0].l_len);
    if(verbose) {
 	printf("Name          Index Length\n");
	printf("Directory           %4d\n", total);
    }

    for (i = 1; i < nslots; i++)
	switch(ldir[i].l_stat) {
	case ACTIVE:
		active++;
		uname = getfname(ldir[i].l_name, ldir[i].l_ext);
		if (filarg (uname))
		    if(verbose)
			printf ("%-12s   %4d %4d\n", uname,
			    wtoi (ldir[i].l_off), wtoi (ldir[i].l_len));
		    else
			printf ("%s\n", uname);
		total += wtoi(ldir[i].l_len);
		break;
	case UNUSED:
		unused++;
		break;
	default:
		deleted++;
	}
    if(verbose) {
	printf("--------------------------\n");
	printf("Total sectors       %4d\n", total);
	printf("\nLibrary %s has %d slots, %d deleted %d active, %d unused\n",
		lib, nslots, deleted, active, unused);
    }

    VOID fclose (lfd);
    not_found ();
}


#ifndef NOEDIT
putdir (f)
FILE *f;
{

#ifdef __GNUC__
	if (fseek(f, 0L, SEEK_SET) == -1)   //<< workaround for gcc
#else
    if (rewind(f) == -1)
#endif
        error("Can't rewind the library file\n");

    if (fwrite ((char *) ldir, DSIZE, nslots, f) != nslots)
	error ("Can't write directory - library may be botched");
}


initdir (f)
FILE *f;
{
    register int    i;
    int     numsecs;
    char    line[80];
	
    static struct ludir blankentry;

/*
    static struct ludir blankentry = {
	UNUSED,
	{ ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
	{ ' ', ' ', ' ' },
    };
*/
	blankentry.l_stat = UNUSED;
    strcpy (blankentry.l_name, "        ");
	strcpy (blankentry.l_ext, "   ");

    for (;;) {
	printf ("Number of slots to allocate: ");
	if (fgets (line, 80, stdin) == NULL)
	    error ("Eof when reading input");
	nslots = atoi (line);
	if (nslots < 1)
	    printf ("Must have at least one!\n");
	else if (nslots > MAXFILES)
	    printf ("Too many slots\n");
	else
	    break;
    }

    numsecs = nslots / SLOTS_SEC;
    nslots = numsecs * SLOTS_SEC;

    for (i = 0; i < nslots; i++)
	ldir[i] = blankentry;
    ldir[0].l_stat = ACTIVE;
    itow (ldir[0].l_len, numsecs);

    putdir (f);
}
#endif


putname (cpmname, unixname)
char   *cpmname, *unixname;
{
    register char  *p1, *p2;

    for (p1 = unixname, p2 = cpmname; *p1; p1++, p2++) {
	while (*p1 == '.') {
	    p2 = cpmname + 8;
	    p1++;
	}
	if (p2 - cpmname < 11)
	    *p2 = islower(*p1) ? toupper(*p1) : *p1;
	else {
	    fprintf (stderr, "%s: name truncated\n", unixname);
	    break;
	}
    }
    while (p2 - cpmname < 11)
	*p2++ = ' ';
}


acopy (fdi, fdo, nsecs)
FILE *fdi, *fdo;
register unsigned int nsecs;
{
    register int    i, c;
    int	    textfile = 1;

    while( nsecs-- != 0) 
	for(i=0; i<SECTOR; i++) {
		c = getc(fdi);
		if( feof(fdi) ) 
			error("Premature EOF\n");
		if( ferror(fdi) )
		    error ("Can't read");
		if( !isascii(c) )
		    textfile = 0;
		if( nsecs != 0 || !textfile || c != CTRLZ) {
			putc(c, fdo);
			if ( ferror(fdo) )
			    error ("write error");
		}
	 }
}


getfiles (name, pflag)
char   *name;
bool	pflag;
{
    FILE *lfd, *ofd;
    register int    i;
    char   *unixname;

    if ((lfd = fopen (name, "rb"))  == NULL)
	cant (name);

    ofd = pflag ? stdout : NULL;
    getdir (lfd);

    for (i = 1; i < nslots; i++) {
	if(ldir[i].l_stat != ACTIVE)
		continue;
	unixname = getfname (ldir[i].l_name, ldir[i].l_ext);
	if (!filarg (unixname))
	    continue;
	fprintf(stderr,"%s", unixname);
	if (ofd != stdout)
	    ofd = fopen (unixname, "wb");
	if (ofd == NULL) {
	    fprintf (stderr, "  - can't create");
	    errcnt++;
	} else {
	    VOID fseek (lfd, (long) wtoi (ldir[i].l_off) * SECTOR, SEEK_SET);
	    acopy (lfd, ofd, wtoi (ldir[i].l_len));
	    if (ofd != stdout) {
			VOID fclose (ofd);
#ifdef USQ
			unsqueeze(unixname);
#endif
#ifdef UNCRUNCH
			uncrunch(unixname);
#endif
		}
		
	}
	// extra close() call, to fix z88dk compiled programs behavior
	if (ofd != stdout)
		VOID fclose (ofd);
	putc('\n', stderr);
    }
    VOID fclose (lfd);
    not_found ();
}


extract(name)
char *name;
{
	getfiles(name, false);
}


print(name)
char *name;
{
	getfiles(name, true);
}


#ifndef NOEDIT
fcopy (ifd, ofd)
FILE *ifd, *ofd;
{
    register int total = 0;
    register int i, n;
    char sectorbuf[SECTOR];


    while ( (n = fread( sectorbuf, 1, SECTOR, ifd)) != 0) {
	if (n != SECTOR)
	    for (i = n; i < SECTOR; i++)
		sectorbuf[i] = CTRLZ;
	if (fwrite( sectorbuf, 1, SECTOR, ofd ) != SECTOR)
		error("write error");
	++total;
    }
    return total;
}


addfil (name, lfd)
char   *name;
FILE *lfd;
{
    FILE	*ifd;
    register int secoffs, numsecs;
    register int i;
	
    if ((ifd = fopen (name, "rb")) == NULL) {
	fprintf (stderr, "%s: can't find to add\n",name);
	errcnt++;
	return;
    }
    if(verbose)
        fprintf(stderr, "%s\n", name);
    for (i = 0; i < nslots; i++) {
	if (equal( getfname (ldir[i].l_name, ldir[i].l_ext), name) ) /* update */
	    break;
	if (ldir[i].l_stat != ACTIVE)
		break;
    }
    if (i >= nslots) {
	fprintf (stderr, "%s: can't add library is full\n",name);
	errcnt++;
	return;
    }

    ldir[i].l_stat = ACTIVE;
    putname (ldir[i].l_name, name);
    VOID fseek(lfd, 0L, SEEK_END);		/* append to end */
    secoffs = ftell(lfd) / 128L;

    itow (ldir[i].l_off, secoffs);
    numsecs = fcopy (ifd, lfd);
    itow (ldir[i].l_len, numsecs);
    VOID fclose (ifd);
}


update (name)
char   *name;
{
    FILE *lfd;
    register int    i;

    if ((lfd = fopen (name, "r+b")) == NULL) {
	    cant (name);
	initdir (lfd);
    }
    else
	getdir (lfd);		/* read old directory */

    if(verbose)
	    fprintf (stderr,"Updating files:\n");
    for (i = 0; i < nfiles; i++)
	addfil (fname[i], lfd);
    if (errcnt == 0)
	putdir (lfd);
    else
	fprintf (stderr, "fatal errors - library not changed\n");
    VOID fclose (lfd);
}


del_entry (lname)
char   *lname;
{
    FILE *f;
    char *unixnm;
    register int    i;

    if ((f = fopen (lname, "r+b")) == NULL)
	cant (lname);

    if (nfiles <= 0)
	error("Filename to delete from Library was not specified");

    getdir (f);
	
    for (i = 0; i < nslots; i++) {
        unixnm = getfname (ldir[i].l_name, ldir[i].l_ext);
	if (!filarg (unixnm))
	    continue;
	ldir[i].l_stat = DELETED;
	if (verbose)
	    printf("Deleted File %s\n",unixnm);
    }

    not_found();
    if (errcnt > 0)
	fprintf (stderr, "errors - library not updated\n");
    else
	putdir (f);
    if (fclose(f) == EOF)
	printf("Updated library file not closed--Is it a Stream File?\n");
}


copymem(dst, src, n)
register char *dst, *src;
register unsigned int n;
{
	while(n-- != 0)
		*dst++ = *src++;
}


copyentry( old, of, new, nf )
struct ludir *old, *new;
FILE *of, *nf;
{
    register int secoffs, numsecs;
    char buf[SECTOR];

    new->l_stat = ACTIVE;
    copymem(new->l_name, old->l_name, 8);
    copymem(new->l_ext, old->l_ext, 3);
    VOID fseek(of, (long) wtoi(old->l_off)*SECTOR, SEEK_SET);
    VOID fseek(nf, 0L, SEEK_END);
    secoffs = ftell(nf) / SECTOR;

    itow (new->l_off, secoffs);
    numsecs = wtoi(old->l_len);
    itow (new->l_len, numsecs);

    while(numsecs-- != 0) {
	if( fread( buf, 1, SECTOR, of) != SECTOR)
	    error("read error");
	if( fwrite( buf, 1, SECTOR, nf) != SECTOR)
	    error("write error");
    }
}


reorg (name)
char  *name;
{
    FILE *olib, *nlib;
    int oldsize;
    register int i, j;
    struct ludir odir[MAXFILES];
    //char tmpname[SECTOR];
	char tmpname[]="lutemp.tmp";

    //strcpy(tmpname,name);

    if( (olib = fopen(name,"rb")) == NULL)
	cant(name);

    if( (nlib = fopen(tmpname, "wb")) == NULL)
	cant(tmpname);

    getdir(olib);
    printf("Old library has %d slots\n", oldsize = nslots);
    for(i = 0; i < nslots ; i++)
	    copymem( (char *) &odir[i], (char *) &ldir[i],
			sizeof(struct ludir));
    initdir(nlib);
    errcnt = 0;

    for (i = j = 1; i < oldsize; i++)
	if( odir[i].l_stat == ACTIVE ) {
	    if(verbose)
		fprintf(stderr, "Copying: %-8.8s.%3.3s\n",
			odir[i].l_name, odir[i].l_ext);
	    copyentry( &odir[i], olib,  &ldir[j], nlib);
	    if (++j >= nslots) {
		errcnt++;
		fprintf(stderr, "Not enough room in new library\n");
		break;
	    }
	}

    VOID fclose(olib);
    putdir(nlib);
    VOID fclose (nlib);

    if(errcnt == 0) {
/*
**	if ( unlink(name) < 0 || link(tmpname, name) < 0) {
**	    VOID unlink(tmpname);
**	    cant(name);
**       }
*/
		VOID remove(name);
		VOID rename(tmpname, name);
    } else {
	fprintf(stderr,"Errors, library not updated\n");
    //VOID delete(tmpname);
	VOID remove(tmpname);
	}

}
#endif


main (argc, argv)
int	argc;
char  **argv;
{
    register char *flagp;
    char   *aname;			/* name of library file */
    int	   (*function)() = NULL;	/* function to do on library */
/* set the function to be performed, but detect conflicts */
#define setfunc(val)	if(function != NULL) conflict(); else function = val

    //cmdname = argv[0];
	cmdname = "LAR";
    if (argc < 3)
	help ();

    aname = argv[2];
    filenames (argc, argv);

    for(flagp = argv[1]; *flagp; flagp++)
	switch (*flagp) {
	case '-':
		break;
	case 't': 
	case 'T': 
	    setfunc(table);
	    break;
	case 'e': 
	case 'E': 
	    setfunc(extract);
	    break;
	case 'p': 
	case 'P': 
	    setfunc(print);
	    break;
#ifndef NOEDIT
	case 'u': 
	case 'U': 
	    setfunc(update);
	    break;
	case 'd': 
	case 'D': 
	    setfunc(del_entry);
	    break;
	case 'r': 
	case 'R': 
	    setfunc(reorg);
	    break;
#endif
	case 'v':
	case 'V':
	    verbose = true;
	    break;
	default: 
	    help ();
    }
	
    if(function == NULL) {
	fprintf(stderr,"No function key letter specified\n");
	help();
    }

    (*function)(aname);
}

