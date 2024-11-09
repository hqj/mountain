#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/times.h>
#include <string.h>
#include "clock.h"


/* /1* Keep track of most recent reading of cycle counter *1/ */
/* static unsigned cyc_hi = 0; */
/* static unsigned cyc_lo = 0; */

/* void access_counter(unsigned *hi, unsigned *lo) */
/* { */
/*   /1* Get cycle counter *1/ */
/*   asm("rdtsc; movl %%edx,%0; movl %%eax,%1" */ 
/*       : "=r" (*hi), "=r" (*lo) */
/*       : /1* No input *1/ */ 
/*       : "%edx", "%eax"); */
/* } */

/* void start_counter() */
/* { */
/*   access_counter(&cyc_hi, &cyc_lo); */
/* } */

/* double get_counter() */
/* { */
/*   unsigned ncyc_hi, ncyc_lo; */
/*   unsigned hi, lo, borrow; */
/*   double result; */
/*   /1* Get cycle counter *1/ */
/*   access_counter(&ncyc_hi, &ncyc_lo); */
/*   /1* Do double precision subtraction *1/ */
/*   lo = ncyc_lo - cyc_lo; */
/*   borrow = lo > ncyc_lo; */
/*   hi = ncyc_hi - cyc_hi - borrow; */
/*   result = (double) hi * (1 << 30) * 4 + lo; */
/*   if (result < 0) { */
/*     fprintf(stderr, "Error: Cycle counter returning negative value: %.0f\n", result); */
/*   } */
/*   return result; */
/* } */

// SPDX-License-Identifier: GPL-2.0
u_int64_t rdtsc(void)
{
    u_int64_t val;

    /*
     * According to ARM DDI 0487F.c, from Armv8.0 to Armv8.5 inclusive, the
     * system counter is at least 56 bits wide; from Armv8.6, the counter
     * must be 64 bits wide.  So the system counter could be less than 64
     * bits wide and it is attributed with the flag 'cap_user_time_short'
     * is true.
     */
    asm volatile("mrs %0, cntvct_el0" : "=r" (val));

    return val;
}

u_int64_t cyc = 0;

void start_counter()
{
	cyc = rdtsc();
}

double get_counter()
{
	u_int64_t cyc_t = rdtsc();

	return cyc_t - cyc;
}

double ovhd()
{
  /* Do it twice to eliminate cache effects */
  int i;
  double result;
  for (i = 0; i < 2; i++) {
    start_counter();
    result = get_counter();
  }
  return result;
}


/* Keep track of clock speed */
double cpu_ghz = 0.0;

/* Get megahertz from /etc/proc */
#define MAXBUF 512

double core_mhz(int verbose) {
    static char buf[MAXBUF];
    FILE *fp = fopen("/proc/cpuinfo", "r");
    cpu_ghz = 0.0;

    if (!fp) {
	fprintf(stderr, "Can't open /proc/cpuinfo to get clock information\n");
	cpu_ghz = 1.0;
	return cpu_ghz * 1000.0;
    }
    while (fgets(buf, MAXBUF, fp)) {
	if (strstr(buf, "cpu MHz")) {
	    double cpu_mhz = 0.0;
	    sscanf(buf, "cpu MHz\t: %lf", &cpu_mhz);
	    cpu_ghz = cpu_mhz / 1000.0;
	    break;
	}
    }
    fclose(fp);
    if (cpu_ghz == 0.0) {
	fprintf(stderr, "Can't open /proc/cpuinfo to get clock information\n");
	cpu_ghz = 1.0;
	return cpu_ghz * 1000.0;
    }
    if (verbose) {
	printf("Processor Clock Rate ~= %.4f GHz (extracted from file)\n", cpu_ghz);
    }
    return cpu_ghz * 1000;
}

double mhz(int verbose) {
    double val = core_mhz(verbose);
    return val;
}



/* Determine clock rate by measuring cycles
   elapsed while sleeping for sleeptime seconds */
double mhz_full(int verbose, int sleeptime)
{
  double rate;
  start_counter();
  sleep(sleeptime);
  rate = get_counter()/(1e6*sleeptime);
  if (verbose) 
    printf("Processor Clock Rate ~= %.1f MHz\n", rate);
  return rate;
}

///* Version using a default sleeptime */
//double mhz(int verbose)
//{
//  return mhz_full(verbose, 2);
//}

/** Special counters that compensate for timer interrupt overhead */

static double cyc_per_tick = 0.0;

#define NEVENT 100
#define THRESHOLD 1000
#define RECORDTHRESH 3000

/* Attempt to see how much time is used by timer interrupt */
static void callibrate(int verbose)
{
  double oldt;
  struct tms t;
  clock_t oldc;
  int e = 0;
  times(&t);
  oldc = t.tms_utime;
  start_counter();
  oldt = get_counter();
  while (e <NEVENT) {
    double newt = get_counter();
    if (newt-oldt >= THRESHOLD) {
      clock_t newc;
      times(&t);
      newc = t.tms_utime;
      if (newc > oldc) {
	double cpt = (newt-oldt)/(newc-oldc);
	if ((cyc_per_tick == 0.0 || cyc_per_tick > cpt) && cpt > RECORDTHRESH)
	  cyc_per_tick = cpt;
	/*
	if (verbose)
	  printf("Saw event lasting %.0f cycles and %d ticks.  Ratio = %f\n",
		 newt-oldt, (int) (newc-oldc), cpt);
	*/
	e++;
	oldc = newc;
      }
      oldt = newt;
    }
  }
  if (verbose)
    printf("Setting cyc_per_tick to %f\n", cyc_per_tick);
}

static clock_t start_tick = 0;

void start_comp_counter() {
  struct tms t;
  if (cyc_per_tick == 0.0)
    callibrate(1);
  times(&t);
  start_tick = t.tms_utime;
  start_counter();
}

double get_comp_counter() {
  double time = get_counter();
  double ctime;
  struct tms t;
  clock_t ticks;
  times(&t);
  ticks = t.tms_utime - start_tick;
  ctime = time - ticks*cyc_per_tick;
  /*
  printf("Measured %.0f cycles.  Ticks = %d.  Corrected %.0f cycles\n",
	 time, (int) ticks, ctime);
  */
  return ctime;
}
