/*
 * data2vcd.c -- convert raw data to VCD file
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>


#define LINE_SIZE	100
#define MAX_TOKENS	20

#define MIN_CODE	'!'
#define MAX_CODE	'~'
#define NUM_CODES	(MAX_CODE - MIN_CODE + 1)


/**************************************************************/


unsigned char data[512][16];

int timeNumber = 0;	/* 1, 10, or 100 */
int timeFactor;		/* timeNumber * timeFactor = sample interval */
char timeUnit[3];	/* s, ms, us, ns, ps, or fs, NUL terminated */
char *module = NULL;	/* name of module */
struct {
  char *name;		/* name of signal */
  int hiIndex;		/* high bit index in raw data vector, 0..127 */
  int loIndex;		/* low bit index in raw data vector, 0..127 */
  char code[3];		/* VCD identifier code for this signal */
} signals[128];
int numSignals = 0;	/* number of signals */


/**************************************************************/

/* error handling, memory allocation */


void error(char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  printf("Error: ");
  vprintf(fmt, ap);
  printf("\n");
  va_end(ap);
  exit(1);
}


void *memAlloc(unsigned int size) {
  void *p;

  p = malloc(size);
  if (p == NULL) {
    error("no memory");
  }
  return p;
}


/**************************************************************/

/* raw data file reader */


void readData(char *dataName) {
  FILE *dataFile;
  int i, j;

  dataFile = fopen(dataName, "r");
  if (dataFile == NULL) {
    error("cannot open data file '%s'", dataName);
  }
  for (i = 0; i < 512; i++) {
    for (j = 0; j < 16; j++) {
      if (fread(&data[i][j], 1, 1, dataFile) != 1) {
        error("cannot read from data file '%s'", dataName);
      }
    }
  }
  fclose(dataFile);
}


/**************************************************************/

/* control file reader and interpreter */


void ctrlError(char *msg, char *file, int line) {
  error("%s in file '%s', line %d", msg, file, line);
}


int isNumber(char *str) {
  if (str == NULL || *str == '\0') {
    return 0;
  }
  while (*str != '\0') {
    if (!isdigit(*str)) {
      return 0;
    }
    str++;
  }
  return 1;
}


int isName(char *str) {
  if (str == NULL || *str == '\0') {
    return 0;
  }
  if (!isalpha(*str) && *str != '_') {
    return 0;
  }
  while (*str != '\0') {
    if (!isalnum(*str) && *str != '_') {
      return 0;
    }
    str++;
  }
  return 1;
}


void numberToCode(int n, char *code) {
  if (n < NUM_CODES) {
    code[0] = n + MIN_CODE;
    code[1] = '\0';
    return;
  }
  n -= NUM_CODES;
  if (n < NUM_CODES * NUM_CODES) {
    code[0] = n / NUM_CODES + MIN_CODE;
    code[1] = n % NUM_CODES + MIN_CODE;
    code[2] = '\0';
    return;
  }
  error("number too big in numberToCode()");
}


void readCtrl(char *ctrlName) {
  FILE *ctrlFile;
  int lineno;
  char line[LINE_SIZE];
  char *p;
  char *tokens[MAX_TOKENS];
  int numTokens;
  int hiIndex, loIndex;

  ctrlFile = fopen(ctrlName, "r");
  if (ctrlFile == NULL) {
    error("cannot open ctrl file '%s'", ctrlName);
  }
  lineno = 0;
  while (fgets(line, LINE_SIZE, ctrlFile) != NULL) {
    lineno++;
    numTokens = 0;
    p = strtok(line, " \t\n");
    while (p != NULL) {
      if (numTokens == MAX_TOKENS) {
        ctrlError("too many tokens", ctrlName, lineno);
      }
      tokens[numTokens++] = p;
      p = strtok(NULL, " \t\n");
    }
    if (numTokens == 0) {
      /* empty line */
      continue;
    }
    if (*tokens[0] == '#') {
      /* comment */
      continue;
    }
    if (strcmp(tokens[0], "timescale") == 0) {
      if (numTokens != 3) {
        ctrlError("wrong number of tokens for 'timescale' directive",
                  ctrlName, lineno);
      }
      if (!isNumber(tokens[1])) {
        ctrlError("'timescale' directive needs a number",
                  ctrlName, lineno);
      }
      timeNumber = atoi(tokens[1]);
      if (timeNumber % 100 == 0) {
        timeFactor = timeNumber / 100;
        timeNumber = 100;
      } else
      if (timeNumber % 10 == 0) {
        timeFactor = timeNumber / 10;
        timeNumber = 10;
      } else {
        timeFactor = timeNumber;
        timeNumber = 1;
      }
      if (strcmp(tokens[2], "s") != 0 &&
          strcmp(tokens[2], "ms") != 0 &&
          strcmp(tokens[2], "us") != 0 &&
          strcmp(tokens[2], "ns") != 0 &&
          strcmp(tokens[2], "ps") != 0 &&
          strcmp(tokens[2], "fs") != 0) {
        ctrlError("'timescale' must use one of (s, ms, us, ns, ps, fs)",
                  ctrlName, lineno);
      }
      strcpy(timeUnit, tokens[2]);
    } else
    if (strcmp(tokens[0], "module") == 0) {
      if (numTokens != 2) {
        ctrlError("wrong number of tokens for 'module' directive",
                  ctrlName, lineno);
      }
      if (!isName(tokens[1])) {
        ctrlError("'module' directive needs a name",
                  ctrlName, lineno);
      }
      module = memAlloc(strlen(tokens[1]) + 1);
      strcpy(module, tokens[1]);
    } else
    if (strcmp(tokens[0], "wire") == 0) {
      if (numTokens != 3 && numTokens != 5) {
        ctrlError("wrong number of tokens for 'wire' directive",
                  ctrlName, lineno);
      }
      if (!isName(tokens[1])) {
        ctrlError("'wire' needs a name",
                  ctrlName, lineno);
      }
      if (!isNumber(tokens[2])) {
        ctrlError("high index must be a number",
                  ctrlName, lineno);
      }
      hiIndex = atoi(tokens[2]);
      if (numTokens == 5) {
        if (strcmp(tokens[3], ":") != 0) {
          ctrlError("separator ':' between high and low index missing",
                    ctrlName, lineno);
        }
        if (!isNumber(tokens[4])) {
          ctrlError("low index must be a number",
                    ctrlName, lineno);
        }
        loIndex = atoi(tokens[4]);
      } else {
        loIndex = hiIndex;
      }
      if (hiIndex < 0 || hiIndex > 127) {
        ctrlError("high index out of range",
                  ctrlName, lineno);
      }
      if (loIndex < 0 || loIndex > 127) {
        ctrlError("low index out of range",
                  ctrlName, lineno);
      }
      if (hiIndex < loIndex) {
        ctrlError("range must be specified as high : low",
                  ctrlName, lineno);
      }
      signals[numSignals].name = memAlloc(strlen(tokens[1]) + 1);
      strcpy(signals[numSignals].name, tokens[1]);
      signals[numSignals].hiIndex = hiIndex;
      signals[numSignals].loIndex = loIndex;
      numberToCode(numSignals, signals[numSignals].code);
      numSignals++;
    } else {
      ctrlError("unknown directive", ctrlName, lineno);
    }
  }
  fclose(ctrlFile);
  if (timeNumber == 0) {
    error("'timescale' directive missing in file '%s'", ctrlName);
  }
  if (module == NULL) {
    error("'module' directive missing in file '%s'", ctrlName);
  }
  if (numSignals == 0) {
    error("'wire' directive(s) missing in file '%s'", ctrlName);
  }
}


/**************************************************************/

/* VCD file writer */


int getDataBitAtTime(int bitno, int time) {
  int byteno;
  int bitshift;

  if (bitno < 0 || bitno >= 128) {
    error("illegal bit number %d in getDataBitAtTime()", bitno);
  }
  if (time < 0 || time >= 512) {
    error("illegal time %d in getDataBitAtTime()", time);
  }
  byteno = 15 - bitno / 8;
  bitshift = bitno % 8;
  return (data[time][byteno] & (1 << bitshift)) ? 1 : 0;
}


void writeVarDefs(FILE *vcdFile) {
  int signo;
  int numBits;

  for (signo = 0; signo < numSignals; signo++) {
    numBits = signals[signo].hiIndex - signals[signo].loIndex + 1;
    fprintf(vcdFile, "$var wire %d %s %s ",
            numBits, signals[signo].code, signals[signo].name);
    if (numBits > 1) {
      fprintf(vcdFile, "[%d:%d] ", numBits - 1, 0);
    }
    fprintf(vcdFile, "$end\n");
  }
}


void writeVarChange(FILE *vcdFile, int signo, int time) {
  int hi, lo;
  int bitno;

  hi = signals[signo].hiIndex;
  lo = signals[signo].loIndex;
  if (hi == lo) {
    /* scalar */
    fprintf(vcdFile, "%c", getDataBitAtTime(hi, time) + '0');
    fprintf(vcdFile, "%s\n", signals[signo].code);
  } else {
    /* vector */
    fprintf(vcdFile, "b");
    for (bitno = hi; bitno >= lo; bitno--) {
      fprintf(vcdFile, "%c", getDataBitAtTime(bitno, time) + '0');
    }
    fprintf(vcdFile, " %s\n", signals[signo].code);
  }
}


void writeVarInits(FILE *vcdFile) {
  int signo;

  for (signo = 0; signo < numSignals; signo++) {
    writeVarChange(vcdFile, signo, 0);
  }
}


void writeVarChanges(FILE *vcdFile) {
  int time;
  int mustWriteTime;
  int signo;
  int hi, lo;
  int bitno;

  for (time = 1; time < 512; time++) {
    mustWriteTime = 1;
    for (signo = 0; signo < numSignals; signo++) {
      /* do we have to dump this signal? */
      hi = signals[signo].hiIndex;
      lo = signals[signo].loIndex;
      for (bitno = hi; bitno >= lo; bitno--) {
        if (getDataBitAtTime(bitno, time - 1) !=
            getDataBitAtTime(bitno, time)) {
          break;
        }
      }
      if (bitno >= lo) {
        /* signal has changed, must dump */
        if (mustWriteTime) {
          /* time not yet written */
          fprintf(vcdFile, "#%d\n", time * timeFactor);
          mustWriteTime = 0;
        }
        writeVarChange(vcdFile, signo, time);
      }
    }
  }
  fprintf(vcdFile, "#%d\n", time * timeFactor);
}


void writeVcd(char *vcdName) {
  FILE *vcdFile;
  time_t seconds;

  vcdFile = fopen(vcdName, "w");
  if (vcdFile == NULL) {
    error("cannot open vcd file '%s'", vcdName);
  }
  /* ---------- */
  seconds = time(NULL);
  fprintf(vcdFile, "$date\n");
  fprintf(vcdFile, "\t%s", ctime(&seconds));
  fprintf(vcdFile, "$end\n");
  /* ---------- */
  fprintf(vcdFile, "$version\n");
  fprintf(vcdFile, "\tdata2vcd converter\n");
  fprintf(vcdFile, "$end\n");
  /* ---------- */
  fprintf(vcdFile, "$timescale\n");
  fprintf(vcdFile, "\t%d %s\n", timeNumber, timeUnit);
  fprintf(vcdFile, "$end\n");
  /* ---------- */
  fprintf(vcdFile, "$scope module %s $end\n", module);
  writeVarDefs(vcdFile);
  fprintf(vcdFile, "$upscope $end\n");
  fprintf(vcdFile, "$enddefinitions $end\n");
  /* ---------- */
  fprintf(vcdFile, "#0\n");
  fprintf(vcdFile, "$dumpvars\n");
  writeVarInits(vcdFile);
  fprintf(vcdFile, "$end\n");
  /* ---------- */
  writeVarChanges(vcdFile);
  /* ---------- */
  fclose(vcdFile);
}


/**************************************************************/

/* main program */


void usage(char *myself) {
  printf("Usage: %s <data file> <ctrl file> <vcd file>\n", myself);
  exit(0);
}


int main(int argc, char *argv[]) {
  char *dataName;
  char *ctrlName;
  char *vcdName;

  if (argc != 4) {
    usage(argv[0]);
  }
  dataName = argv[1];
  ctrlName = argv[2];
  vcdName = argv[3];
  readData(dataName);
  readCtrl(ctrlName);
  writeVcd(vcdName);
  return 0;
}
