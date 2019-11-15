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
int silent = 0;

typedef struct line_s line_t;
struct line_s
{
   line_t *next;
   double x1,
     y1;
   double x2,
     y2;
};

typedef struct point_s point_t;
struct point_s
{
   point_t *next;
   double x,
     y;
};

typedef struct path_s path_t;
struct path_s
{
   path_t *next;
   point_t *start,
    *end;
};

line_t *lines = NULL;
int linecount = 0;
path_t *paths = NULL;
int pathcount = 0;

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
   double linewidth = 0;
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
         {"line-width", 0, POPT_ARG_DOUBLE, &linewidth, 0, "Line with to capture", "mm"},
         {"g1", 0, POPT_ARG_NONE, &g1, 0, "Use G1 for skipping over"},
         {"neg", 0, POPT_ARG_NONE, &sign, 0, "Use negative X/Y"},
         {"scale", 'S', POPT_ARG_DOUBLE, &scale, 0, "Scale", "N"},
         {"debug", 'v', POPT_ARG_NONE, &debug, 0, "Debug"},
         {"silent", 'q', POPT_ARG_NONE, &silent, 0, "Silent"},
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
       "/==={(        )cvs print}def/setlinewidth{(W)=== =}bind def/stroke{flattenpath{transform(M)=== round cvi ===(,)=== round cvi =}{transform(L)=== round cvi ===(,)=== round cvi =}{}{(Z)=}pathforall newpath}bind def/showpage{(X)= showpage}bind def\n");
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
   double line = 0;
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
   void spin (path_t * p, point_t * q)
   {                            // Cycle path to new start
      if (p->start->x != p->end->x || p->start->y != p->end->y)
         return;                // Not a loop;
      point_t *z = p->start;
      p->start = z->next;       // Remove duplicate point
      point_t *new = q;
      while (q->next)
         q = q->next;
      q->next = p->start;
      while (q->next != new)
         q = q->next;
      z->x = new->x;            // Loop
      z->y = new->y;
      z->next = NULL;
      q->next = z;
      p->end = z;
      p->start = new;
   }
   line_t *swap (line_t * l)
   {                            // Reverse line
      double x = l->x1;
      l->x1 = l->x2;
      l->x2 = x;
      double y = l->y1;
      l->y1 = l->y2;
      l->y2 = y;
      return l;
   }
   fprintf (o, "M3S%d\n", speed);
   {                            // Process paths
      double firstx = 0,
         firsty = 0;
      double prevx = 0,
         prevy = 0;
      while (fgets ((char *) temp, sizeof (temp), i))
      {
         char *l = temp;
         char c = *l++;
         double y = strtod (l, &l) * scale * 2.54 / 72;
         if (*l == ',')
            l++;
         double x = strtod (l, &l) * scale * 2.54 / 72;
         if (c == 'W')
         {
            line = round(y*100)/10;  // (mm)
            if(debug)fprintf (stderr, "Width %lfmm\n", line);
            continue;
         }
	 if(linewidth&&linewidth!=line)continue; // Not the width we are after
         if (c == 'M')
         {                      // Start path
            firstx = prevx = x;
            firsty = prevy = y;
            continue;
         }
         if (c == 'L')
         {
            if (x == prevx && y == prevy)
               continue;
            linecount++;
            line_t *l = malloc (sizeof (*l));
            if (!l)
               errx (1, "malloc");
            memset (l, 0, sizeof (*l));
            l->x1 = prevx;
            l->y1 = prevy;
            l->x2 = x;
            l->y2 = y;
            prevx = x;
            prevy = y;
            l->next = lines;
            lines = l;
            continue;
         }
         if (c == 'Z')
         {
            if (x == firstx && y == firsty)
               continue;
            linecount++;
            line_t *l = malloc (sizeof (*l));
            if (!l)
               errx (1, "malloc");
            memset (l, 0, sizeof (*l));
            l->x1 = prevx;
            l->y1 = prevy;
            l->x2 = firstx;
            l->y2 = firsty;
            l->next = lines;
            lines = l;
            continue;
         }
         if (c == 'X')
            break;              // End of page
         warnx ("Unknown: %s", temp);
      }
   }
   if (debug)
   {
      line_t *l;
      for (l = lines; l; l = l->next)
         fprintf (stderr, "%lf/%lf %lf/%lf\n", l->x1, l->y1, l->x2, l->y2);
   }
   {                            // Make paths
      path_t *path = NULL;
      while (lines)
      {
         if (!path)
         {                      // New path - pick any line
            line_t *l = lines;
            lines = l->next;
            if (debug)
               fprintf (stderr, "Make new path %lf/%lf %lf/%lf\n", l->x1, l->y1, l->x2, l->y2);
            pathcount++;
            path = malloc (sizeof (*path));
            if (!path)
               errx (1, "malloc");
            memset (path, 0, sizeof (*path));
            path->next = paths;
            paths = path;
            point_t *q = malloc (sizeof (*q));
            if (!q)
               errx (1, "malloc");
            memset (q, 0, sizeof (*q));
            q->x = l->x1;
            q->y = l->y1;
            path->start = q;
            q = malloc (sizeof (*q));
            if (!q)
               errx (1, "malloc");
            memset (q, 0, sizeof (*q));
            q->x = l->x2;
            q->y = l->y2;
            path->start->next = path->end = q;
            free (l);
            continue;
         }
         line_t *find (double x, double y)
         {                      // Find (and unlink) a line that hits a point, returns with x1/y1 matching
            line_t **ll = &lines;
            while (*ll)
            {
               line_t *l = *ll;
               if (l->x1 == x && l->y1 == y)
               {                // Found
                  *ll = l->next;
                  return l;
               }
               if (l->x2 == x && l->y2 == y)
               {                // Found
                  *ll = l->next;
                  return swap (l);
               }
               ll = &(l->next);
            }
            return NULL;
         }
         line_t *l = NULL;
         if (path->start->x == path->end->x && path->start->y == path->end->y)
         {                      // Loop, find line to join anywhere
            point_t *q;
            for (q = path->start; q; q = q->next)
            {
               if ((l = find (q->x, q->y)))
               {                // Found point in the path
                  if (q != path->start)
                     spin (path, q);
                  break;
               }
            }
         }
         if (!l)
            l = find (path->start->x, path->start->y);
         if (l)
         {                      // Add to start
            if (debug)
               fprintf (stderr, "Add to start  %lf/%lf %lf/%lf\n", l->x1, l->y1, l->x2, l->y2);
            point_t *q = malloc (sizeof (*q));
            if (!q)
               errx (1, "malloc");
            memset (q, 0, sizeof (*q));
            q->x = l->x2;
            q->y = l->y2;
            q->next = path->start;
            path->start = q;
            free (l);
         } else if ((l = find (path->end->x, path->end->y)))
         {
            if (debug)
               fprintf (stderr, "Add to end    %lf/%lf %lf/%lf\n", l->x1, l->y1, l->x2, l->y2);
            point_t *q = malloc (sizeof (*q));
            if (!q)
               errx (1, "malloc");
            memset (q, 0, sizeof (*q));
            q->x = l->x2;
            q->y = l->y2;
            path->end->next = q;
            path->end = q;
            free (l);
         } else
            path = NULL;        // New path
      }
   }
   if (!silent)
      fprintf (stderr, "Lines %d Paths %d\n", linecount, pathcount);
   // Output
   up (zskip);
   while (paths)
   {
      path_t **best = NULL;
      point_t *beststart = NULL;
      char bestrev = 0;
      {
         double bestdist = 0;
         double dist (double x, double y)
         {
            double d = fabs (x - lastx) * fabs (x - lastx) + fabs (y - lasty) * fabs (y - lasty);
            return d;
         }
         path_t **pp = &paths;
         while (*pp)
         {
            path_t *p = (*pp);
            double d;
            if (p->start->x == p->end->x && p->start->y == p->end->y)
            {                   // Loop, start anywhere
               point_t *q;
               for (q = p->start; q; q = q->next)
                  if ((d = dist (q->x, q->y)) < bestdist || !best)
                  {
                     best = pp;
                     bestdist = d;
                     bestrev = 0;
                     beststart = q;
                  }
            }
            if ((d = dist (p->start->x, p->start->y)) < bestdist || !best)
            {
               best = pp;
               bestdist = d;
               bestrev = 0;
               beststart = NULL;
            }
            if ((d = dist (p->end->x, p->end->y)) < bestdist || !best)
            {
               best = pp;
               bestdist = d;
               bestrev = 1;
               beststart = NULL;
            }
            pp = &(p->next);
         }
      }
      if (!best || !*best)
         errx (1, "Bad best check");
      path_t *p = (*best);
      *best = p->next;          // Unlink
      if (debug)
         fprintf (stderr, "Best %lf/%lf %lf/%lf\n", p->start->x, p->start->y, p->end->x, p->end->y);
      if (beststart && beststart != p->start)
         spin (p, beststart);
      else if (bestrev)
      {                         // reverse loop
         point_t *new = NULL,
            *q = p->start;
         while (q)
         {
            point_t *n = q->next;
            q->next = new;
            new = q;
            q = n;
         }
         p->start = new;
      }
      // Output path
      point_t *q = p->start;
      skip (q->x, q->y);
      while (q)
      {
         point_t *n = q->next;
         cut (q->x, q->y);
         free (q);
         q = n;
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
