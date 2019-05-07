// EPS 2 GCODE (for PCB milling)
// (c) 2019 Adrian Kennard

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <popt.h>
#include <err.h>

int debug = 0;

int
main (int argc, const char *argv[])
{
   int fcut = 30;
   int fdown = 0;
   int fup = 0;
   int fskip = 500;
   int speed = 2000;
   int places = 3;
   int g1 = 0;
   int sign = 0;
   const char *infile = NULL;
   const char *outfile = NULL;
   double zcut = -0.05;         // Default cut depth (assuming you have some auto levelling)
   double zskip = 5;            // Well above
   double zclear = 0.5;         // Just above
   double scale = 1;
   {                            // POPT
      poptContext optCon;       // context for parsing command-line options
      const struct poptOption optionsTable[] = {
         {"f-cut", 'f', POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &fcut, 0, "Feed rate cutting", "N"},
         {"f-down", 0, POPT_ARG_INT, &fdown, 0, "Feed rate down", "N"},
         {"f-up", 0, POPT_ARG_INT, &fup, 0, "Feed rate up", "N"},
         {"f-skip", 0, POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &fskip, 0, "Feed rate skipping (may be ignored if using G0)", "N"},
         {"speed", 's', POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &speed, 0, "Cutting speed", "rpm"},
         {"in-file", 'i', POPT_ARG_STRING, &infile, 0, "Input EPS", "filename"},
         {"out-file", 'o', POPT_ARG_STRING, &outfile, 0, "Output GCODE", "filename"},
         {"z-cut", 0, POPT_ARG_DOUBLE | POPT_ARGFLAG_SHOW_DEFAULT, &zcut, 0, "Cut depth", "mm"},
         {"z-skip", 0, POPT_ARG_DOUBLE | POPT_ARGFLAG_SHOW_DEFAULT, &zskip, 0, "Skip depth", "mm"},
         {"places", 0, POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &places, 0, "Decimal places", "N"},
         {"g1", 0, POPT_ARG_NONE, &g1, 0, "Use G1 for skipping over"},
         {"neg", 0, POPT_ARG_NONE, &sign, 0, "Use negative X/Y"},
         {"scale", 'S', POPT_ARG_DOUBLE, &scale, 0, "Scale", "N"},
         {"debug", 'v', POPT_ARG_NONE, &debug, 0, "Debug"},
         POPT_AUTOHELP {}
      };

      optCon = poptGetContext (NULL, argc, argv, optionsTable, 0);
      poptSetOtherOptionHelp (optCon, "[infile] [outfile]");

      int c;
      if ((c = poptGetNextOpt (optCon)) < -1)
         errx (1, "%s: %s\n", poptBadOption (optCon, POPT_BADOPTION_NOALIAS), poptStrerror (c));

      if (!infile && poptPeekArg (optCon))
         infile = poptGetArg (optCon);
      if (!outfile && poptPeekArg (optCon))
         outfile = poptGetArg (optCon);

      if (poptPeekArg (optCon))
      {
         poptPrintUsage (optCon, stderr, 0);
         return -1;
      }
      poptFreeContext (optCon);
   }
   if (sign)
      sign = -1;
   else
      sign = 1;
   // Defaults
   if (!fdown)
      fdown = fcut;
   if (!fup)
      fup = fskip;
   // Get EPS and modify
   FILE *i = stdin;
   if (infile && strcmp (infile, "-"))
      i = fopen (infile, "r");
   if (!i)
      err (1, "Cannot open input");
   char temp[65536];
   char epsfile[1024] = "/tmp/XXXXXX.eps";
   FILE *g = fdopen (mkstemps (epsfile, 4), "w");
   if (!g)
      err (1, "Cannot open temp file");
   while (fgets ((char *) temp, sizeof (temp), i))
   {                            // PS comments
      fprintf (g, "%s", (char *) temp);
      if (*temp != '%')
         break;                 // Rest of file
   }
   fprintf
      (g,
       "/==={(        )cvs print}def/stroke{flattenpath{transform(M)=== round cvi ===(,)=== round cvi =}{transform(L)=== round cvi ===(,)=== round cvi =}{}{(Z)=}pathforall newpath}bind def/showpage{(X)= showpage}bind def\n");
   ssize_t l;
   while ((l = fread (temp, 1, sizeof (temp), i)) > 0)
      l = fwrite (temp, 1, l, g);       // Rest of file
   fclose (g);
   fclose (i);
   // Run through GS
   char vectorfile[1024] = "/tmp/XXXXXX.vector";
   close (mkstemps (vectorfile, 7));
   snprintf (temp, sizeof (temp), "/usr/bin/gs -q -dBATCH -dNOPAUSE -sDEVICE=eps2write -sOutputFile=/dev/null %s > %s", epsfile,
             vectorfile);
   if (system ((char *) temp))
      return 0;
   // Process result to GCODE
   i = fopen (vectorfile, "r");
   if (!i)
      err (1, "Cannot open vector file");
   FILE *o = stdout;
   if (outfile && strcmp (outfile, "i"))
      o = fopen (outfile, "w");
   if (!o)
      err (1, "Cannot open output");
   fprintf (o, "G90\nG21\n");
   double startx = 0,
      starty = 0,
      lastx = 0,
      lasty = 0;
   int isup = 0,
      lastf = 0;
   void setx (double x)
   {
      if (x != lastx)
         fprintf (o, "X%.*lf", places, (lastx = x) * sign);
   }
   void sety (double y)
   {
      if (y != lasty)
         fprintf (o, "Y%.*lf", places, (lasty = y) * sign);
   }
   void setf (double f)
   {
      if (f != lastf)
         fprintf (o, "F%d", lastf = f);
   }
   void up (void)
   {
      if (isup)
         return;
      fprintf (o, "G1Z%.*lf", places, zclear);
      setf (fup);
      fprintf (o, "\nG%dZ%.*lf", g1, places, zskip);
      setf (fskip);
      fprintf (o, "\n");
      isup = 1;
   }
   void down (void)
   {
      if (!isup)
         return;
      fprintf (o, "G%dZ%.*lf", g1, places, zclear);
      setf (fskip);
      fprintf (o, "\nG1Z%.*lf", places, zcut);
      setf (fdown);
      fprintf (o, "\n");
      isup = 0;

   }
   void skip (double x, double y)
   {
      if (lastx == x && lasty == y)
         return;
      up ();
      fprintf (o, "G%d", g1);
      setx (x);
      sety (y);
      setf (fskip);
      fprintf (o, "\n");
      lastx = x;
      lasty = y;
   }
   void cut (double x, double y)
   {
      if (lastx == x && lasty == y)
         return;
      down ();
      fprintf (o, "G1");
      setx (x);
      sety (y);
      setf (fcut);
      fprintf (o, "\n");
      lastx = x;
      lasty = y;

   }
   up ();
   fprintf (o, "M3S%d\n", speed);
   while (fgets ((char *) temp, sizeof (temp), i))
   {
      char *l = temp;
      char c = *l++;
      double y = strtod (l, &l) * scale * 2.54 / 72;
      if (*l == ',')
         l++;
      double x = strtod (l, &l) * scale * 2.54 / 72;
      if (c == 'M')
      {
         startx = x;
         starty = y;
         skip (x, y);
      }
      if (c == 'L')
         cut (x, y);
      if (c == 'Z')
         cut (startx, starty);
   }
   up ();
   fprintf (o, "M5\n");
   skip (0, 0);
   fprintf (o, "M30\n");
   fclose (o);
   fclose (i);
   if (!debug)
      unlink (epsfile);
   if (!debug)
      unlink (vectorfile);
   return 0;
}
