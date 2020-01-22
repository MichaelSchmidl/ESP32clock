#include "daylightCalc.h"


static double pi = 3.14159;
static double degs;
static double rads;

static double L,g,daylen;
static double SunDia = 0.53;     // Sunradius degrees

static double AirRefr = 34.0/60.0; // athmospheric refraction degrees //

//   Get the days to J2000
//   h is UT in decimal hours
//   FNday only works between 1901 to 2099 - see Meeus chapter 7
static double FNday (int y, int m, int d, float h)
{
	long int luku = - 7 * (y + (m + 9)/12)/4 + 275*m/9 + d;
	// type casting necessary on PC DOS and TClite to avoid overflow
	luku+= (long int)y*367;
	return (double)luku - 730531.5 + h/24.0;
};


//   the function below returns an angle in the range
//   0 to 2*pi
static double FNrange (double x)
{
    double b = 0.5*x / pi;
    double a = 2.0*pi * (b - (long)(b));
    if (a < 0) a = 2.0*pi + a;
    return a;
};


// Calculating the hourangle
//
static double f0(double lat, double declin)
{
	double fo,dfo;
	// Correction: different sign at S HS
	dfo = rads*(0.5*SunDia + AirRefr); if (lat < 0.0) dfo = -dfo;
	fo = tan(declin + dfo) * tan(lat*rads);
	if (fo>0.99999) fo=1.0; // to avoid overflow //
	fo = asin(fo) + pi/2.0;
	return fo;
};


// Calculating the hourangle for twilight times
//
static double f1(double lat, double declin)
{
	double fi,df1;
	// Correction: different sign at S HS
	df1 = rads * 6.0; if (lat < 0.0) df1 = -df1;
	fi = tan(declin + df1) * tan(lat*rads);
	if (fi>0.99999) fi=1.0; // to avoid overflow //
	fi = asin(fi) + pi/2.0;
	return fi;
};


//   Find the ecliptic longitude of the Sun
static double FNsun (double d)
{
	//   mean longitude of the Sun
	L = FNrange(280.461 * rads + .9856474 * rads * d);
	//   mean anomaly of the Sun
	g = FNrange(357.528 * rads + .9856003 * rads * d);
	//   Ecliptic longitude of the Sun
	return FNrange(L + 1.915 * rads * sin(g) + .02 * rads * sin(2 * g));
};


// Display decimal hours in hours and minutes
static void showhrmn(double dhr)
{
	int hr,mn;
	hr=(int) dhr;
	mn = (dhr - (double) hr)*60;

	printf("%0d:%0d",hr,mn);
};


int isSunDown( double latit, double longit, struct tm timeinfo )
{
	float tzone;
	double y;
	double m;
	double day;
	double h;
	double d,lambda;
	double obliq,alpha,delta,LL,equation,ha,hb,twx;
	double twam,altmax,noont,settm,riset,twpm;
	time_t sekunnit;

	y = timeinfo.tm_year + 1900;
	m = timeinfo.tm_mon + 1;
	day = timeinfo.tm_mday;
	tzone = timeinfo.tm_isdst ? 2.0 : 1.0;

	degs = 180.0/pi;
	rads = pi/180.0;
    h = 12;
	d = FNday(y, m, day, h);

	//   Use FNsun to find the ecliptic longitude of the
	//   Sun

	lambda = FNsun(d);

	//   Obliquity of the ecliptic

	obliq = 23.439 * rads - .0000004 * rads * d;

	//   Find the RA and DEC of the Sun

	alpha = atan2(cos(obliq) * sin(lambda), cos(lambda));
	delta = asin(sin(obliq) * sin(lambda));

	// Find the Equation of Time
	// in minutes
	// Correction suggested by David Smith
	LL = L - alpha;
	if (L < pi) LL += 2.0*pi;
	equation = 1440.0 * (1.0 - LL / pi/2.0);
	ha = f0(latit,delta);
	hb = f1(latit,delta);
	twx = hb - ha;	// length of twilight in radians
	twx = 12.0*twx/pi;		// length of twilight in hours
//	printf("ha= %.2f   hb= %.2f \n",ha,hb);
	// Conversion of angle to hours and minutes //
	daylen = degs*ha/7.5;
	     if (daylen<0.0001) {daylen = 0.0;}
	// arctic winter     //

	riset = 12.0 - 12.0 * ha/pi + tzone - longit/15.0 + equation/60.0;
	settm = 12.0 + 12.0 * ha/pi + tzone - longit/15.0 + equation/60.0;
	noont = riset + 12.0 * ha/pi;
	altmax = 90.0 + delta * degs - latit;
	// Correction for S HS suggested by David Smith
	// to express altitude as degrees from the N horizon
	if (latit < delta * degs) altmax = 180.0 - altmax;

	twam = riset - twx;	// morning twilight begin
	twpm = settm + twx;	// evening twilight end

	if (riset > 24.0) riset-= 24.0;
	if (settm > 24.0) settm-= 24.0;

#if 0
	printf("  year  : %d \n",(int)y);
	printf("  month : %d \n",(int)m);
	printf("  day   : %d \n\n",(int)day);
	printf("Days since Y2K :  %d \n",(int)d);

	printf("Latitude :  %3.1f, longitude: %3.1f, timezone: %3.1f \n",(float)latit,(float)longit,(float)tzone);
	printf("Declination   :  %.2f \n",delta * degs);
	printf("Daylength     : "); showhrmn(daylen); puts(" hours \n");
	printf("Civil twilight: ");
	showhrmn(twam); puts("");
	printf("Sunrise       : ");
	showhrmn(riset); puts("");

	printf("Sun altitude ");
	// Amendment by D. Smith
	printf(" %.2f degr",altmax);
	printf(latit>=0.0 ? " South" : " North");
	printf(" at noontime "); showhrmn(noont); puts("");
	printf("Sunset        : ");
	showhrmn(settm);  puts("");
	printf("Civil twilight: ");
	showhrmn(twpm);  puts("\n");
#endif
	double now = (double)timeinfo.tm_hour + ((double)timeinfo.tm_min / 60.0);
	if (( now < riset ) || ( now > settm ))
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

