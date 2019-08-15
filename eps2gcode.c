// EPS 2 GCODE (for PCB milling)
// (c) 2019 Adrian Kennard

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <popt.h>
#include <err.h>
#include <math.h>
#include <malloc.h>

int debug = 0;

typedef struct path_s path_t;
typedef struct point_s point_t;

struct point_s
{
   point_t *next;
   double x,
     y;
};

struct path_s
{
   path_t *next;
   double sx,
     sy;
   double ex,
     ey;
   point_t *points;
};

path_t *paths = NULL;

int
main (int argc, const char *argv[])
{
   int fcut = 30;
   int fdown = 0;
   int fup = 0;
   int fskip = 500;
   int speed = 2000;
   int steps = 400;             // Default for 8 micro steps on 4mm/rev 1.8 degrees
   int g1 = 0;
   int sign = 0;
   const char *infile = NULL;
   const char *outfile = NULL;
   double zcut = -0.05;         // Default cut depth (assuming you have some auto levelling)
   double zskip = 5;            // Well above
   double zclear = 0.5;         // Just above
   double xslack = 0;
   double zpark = 25;
   double yslack = 0;
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
         {"z-park", 0, POPT_ARG_DOUBLE | POPT_ARGFLAG_SHOW_DEFAULT, &zskip, 0, "Park for tool change", "mm"},
         {"steps", 0, POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &steps, 0, "Steps per mm", "N"},
         {"x-slack", 0, POPT_ARG_DOUBLE, &xslack, 0, "X slack", "mm"},
         {"y-slack", 0, POPT_ARG_DOUBLE, &yslack, 0, "Y slack", "mm"},
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
   double lastx = 0,
      lasty = 0;
   int isup = 0,
      lastf = 0;
   char *decimal (double x)
   {
      static char temp[20];
      char *p;
      if (steps)
         x = roundl (x * steps) / steps;
      sprintf (temp, "%.6f", x);
      p = temp + strlen (temp);
      while (p > temp && p[-1] == '0')
         p--;
      if (p > temp && p[-1] == '.')
         p--;
      *p = 0;
      return temp;
   }
   double lastdx = 0,
      lastdy = 0,
      dx = 0,
      dy = 0;                   // Slack adjust
   void setx (double x)
   {
      if (x != lastx || lastdx != dx)
         fprintf (o, "X%s", decimal (((lastx = x) + (lastdx = dx)) * sign));
   }
   void sety (double y)
   {
      if (y != lasty || lastdy != dy)
         fprintf (o, "Y%s", decimal (((lasty = y) + (lastdy = dy)) * sign));
   }
   void setf (double f)
   {
      if (f != lastf)
         fprintf (o, "F%d", lastf = f);
   }
   void up (double z)
   {
      if (isup)
         return;
      fprintf (o, "G1Z%s", decimal (zclear));
      setf (fup);
      fprintf (o, "\nG%dZ%s", g1, decimal (z));
      setf (fskip);
      fprintf (o, "\n");
      isup = 1;
   }
   void down (void)
   {
      if (!isup)
         return;
      fprintf (o, "G%dZ%s", g1, decimal (zclear));
      setf (fskip);
      fprintf (o, "\nG1Z%s", decimal (zcut));
      setf (fdown);
      fprintf (o, "\n");
      isup = 0;

   }
   void skip (double x, double y)
   {
      if (lastx == x && lasty == y)
         return;
      up (zskip);
      fprintf (o, "G%d", g1);
      if (x < lastx)
         dx = -xslack / 2;
      else if (x > lastx)
         dx = xslack / 2;
      if (y < lasty)
         dy = -yslack / 2;
      else if (y > lasty)
         dy = yslack / 2;
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
      if (x < lastx)
         dx = -xslack / 2;
      else if (x > lastx)
         dx = xslack / 2;
      if (y < lasty)
         dy = -yslack / 2;
      else if (y > lasty)
         dy = yslack / 2;
      if (x != lastx && y != lasty && (lastdx != dx || lastdy != dy))
      {                         // Slack adjust for diagonal before we move
         down ();
         fprintf (o, "G1");
         setx (lastx);
         sety (lasty);
         setf (fcut);
         fprintf (o, "\n");
      }
      down ();
      fprintf (o, "G1");
      setx (x);
      sety (y);
      setf (fcut);
      fprintf (o, "\n");
      lastx = x;
      lasty = y;

   }
   fprintf (o, "M3S%d\n", speed);
   // Process paths
   path_t *path = NULL;
   point_t *point = NULL;
   while (fgets ((char *) temp, sizeof (temp), i))
   {
      char *l = temp;
      char c = *l++;
      double y = strtod (l, &l) * scale * 2.54 / 72;
      if (*l == ',')
         l++;
      double x = strtod (l, &l) * scale * 2.54 / 72;
      if (c == 'M' || !point)
      {                         // Start path
         path_t *p = malloc (sizeof (*p));
         if (!p)
            errx (1, "malloc");
         memset (p, 0, sizeof (*p));
         if (path)
            path->next = p;
         else
            paths = p;
         p->sx = x;
         p->sy = y;
         path = p;
         point = malloc (sizeof (*point));
         if (!point)
            errx (1, "malloc");
         memset (point, 0, sizeof (*point));
         point->x = x;
         point->y = y;
         path->points = point;
         continue;
      }
      if (c == 'L')
      {
         point_t *p = malloc (sizeof (*p));
         if (!p)
            errx (1, "malloc");
         memset (p, 0, sizeof (*p));
         p->x = x;
         p->y = y;
         path->ex = p->x;
         path->ey = p->y;
         point->next = p;
         point = p;
         continue;
      }
      if (c == 'Z')
      {
         point_t *p = malloc (sizeof (*p));
         if (!p)
            errx (1, "malloc");
         memset (p, 0, sizeof (*p));
         p->x = path->sx;
         p->y = path->sy;
         path->ex = p->x;
         path->ey = p->y;
         point->next = p;
         point = p;
         point = NULL;
         continue;
      }
      if (c == 'X')
         break;                 // End of page
      warnx ("Unknown: %s", temp);
   }
   // Output
   up (zskip);
   while (paths)
   {
      path_t **best = NULL;
      point_t *bestloop = NULL;
      char bestrev = 0;
      {
         path_t **p = &paths;
         double bestdist = 0;
         double dist (double x, double y)
         {
            return fabs (x - lastx) * fabs (x - lastx) + fabs (y - lasty) * fabs (y - lasty);
         }
         while (*p)
         {
            double d;
            if ((*p)->sx == (*p)->ex && (*p)->sy == (*p)->ey)
            {                   // Loop
               point_t *q;
               for (q = (*p)->points; q; q = q->next)
                  if ((d = dist (q->x, q->y)) < bestdist || !best)
                  {
                     best = p;
                     bestdist = d;
                     bestrev = 0;
                     bestloop = q;
                  }
            } else
            {                   // Non loop, just check ends
               if ((d = dist ((*p)->sx, (*p)->sy)) < bestdist || !best)
               {
                  best = p;
                  bestdist = d;
                  bestrev = 0;
                  bestloop = NULL;
               }
               if ((d = dist ((*p)->ex, (*p)->ey)) < bestdist || !best)
               {
                  best = p;
                  bestdist = d;
                  bestrev = 1;
                  bestloop = NULL;
               }
            }
            p = &((*p)->next);
         }
      }
      if (!best || !*best)
         errx (1, "Bad best check");
      path_t *p = *best;
      *best = p->next;
      if (bestrev)
      {
         point_t *points = NULL;
         point_t *q = p->points;
         while (q)
         {
            point_t *n = q->next;
            q->next = points;
            points = q;
            q = n;
         }
         p->points = points;
      } else if (bestloop && bestloop != p->points)
      {                         // Start mid loop;
         point_t *q = p->points;
         while (q && q->next != bestloop)
            q = q->next;
         if (q)
         {
            q->next = malloc (sizeof (*q));
            memset (q = q->next, 0, sizeof (*q));
            q->x = bestloop->x;
            q->y = bestloop->y;
            q = p->points;
            p->points = bestloop;
            while (bestloop->next)
               bestloop = bestloop->next;
            bestloop->next = q;
         }
      }
      point_t *q = p->points;
      skip (q->x, q->y);
      while (p->points)
      {
         q = p->points->next;
         free (p->points);
         p->points = q;
         if (q)
            cut (q->x, q->y);
      }
      free (p);
   }
   up (zpark);
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
