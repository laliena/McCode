#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/Paned.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/MenuButton.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/SmeBSB.h>
#include <X11/Xaw/SmeLine.h>
#include <X11/Xaw/Viewport.h>
#include <X11/Xaw/Dialog.h>
#include <X11/cursorfont.h>

#include "XaPgplot.h"
#include "cpgplot.h"

/*
 * Make the demo backwardly compatible with older versions of X.
 */
#if XtSpecificationRelease <= 4
#define XtSetLanguageProc(a,b,c) (void)((a),(b),(c))
#endif

/*
 * Gray-scale images of multiple analytic 2D functions will be supported.
 * Each 2D function will be encapsulated in a C function of the
 * following type.
 */
#define IMAGE_FN(fn) float (fn)(float x, float y)
/*
 * Define a macro for prototyping and defining XtCallbackProc functions.
 */
#define CALL_FN(fn) void (fn)(Widget w, XtPointer client_data, XtPointer call_data)
/*
 * List the prototypes of the available 2D-function functions.
 */
static IMAGE_FN(sinc_fn);
static CALL_FN(sinc_callback);
static IMAGE_FN(gaus_fn);
static CALL_FN(gaus_callback);
static IMAGE_FN(ring_fn);
static CALL_FN(ring_callback);
static IMAGE_FN(sin_angle_fn);
static CALL_FN(sin_angle_callback);
static IMAGE_FN(cos_radius_fn);
static CALL_FN(cos_radius_callback);
static IMAGE_FN(star_fn);
static CALL_FN(star_callback);

/* Color table menu callbacks */

static CALL_FN(grey_callback);
static CALL_FN(rainbow_callback);
static CALL_FN(heat_callback);
static CALL_FN(aips_callback);

/* Set the default image size */

enum {MAP_SIZE=129};

/* Set the number of points plotted per slice */

enum {SLICE_SIZE=100};

/*
 * Declare a type to hold a single X,Y coordinate.
 */
typedef struct {
  float x, y;       /* World coordinates */
} Vertex;

/*
 * Declare the object type that will contain the context of the
 * image and slice plots.
 */
typedef struct {
  Widget w_coord;     /* Coordinate-display label widget */
  Widget w_image;     /* The gray-scale image widget */
  Widget w_slice;     /* The slice-plot image widget */
  float *image;       /* The gray-scale image array */
  float *slice;       /* The slice compilation array */
  float scale;        /* Coversion factor pixels -> coords */
  int image_size;     /* The number of pixels along each side of the image */
  int slice_size;     /* The length of the slice array */
  int xa,xb;          /* Min and max X pixel coordinates */
  int ya,yb;          /* Min and max Y pixel coordinates */
  float datamin;      /* The minimum data value in image[] */
  float datamax;      /* The maximum data value in image[] */
  IMAGE_FN(*fn);      /* The function to be displayed */
  Vertex va;          /* The start of the latest slice line */
  Vertex vb;          /* The end of the latest slice line */
  Widget w_top;       /* The top-level widget of the application */
  Cursor busy;        /* The cursor to display when un-responsive */
} Image;

/*
 * Image object contructor and destructor functions.
 */
static Image *new_Image(unsigned image_size, unsigned slice_size,
			IMAGE_FN(*fn), Widget parent, Widget w_top);
static Image *del_Image(Image *im);

/*
 * Image and slice display functions.
 */
static void display_fn(Image *im, IMAGE_FN(*fn));
static void display_image(Image *im, int id);
static void display_slice(Image *im, Vertex *va, Vertex *vb);
static void display_help(Image *im);
static void recolor_image(Image *im, float *lev, float *r, float *g, float *b,
			  int n);

/*
 * The following structure is used to describe menu fields to
 * CreateOptionMenu() and CreatePulldownMenu().
 * Note that title options are denoted by setting callback=NULL,
 * and that separators are specified by setting label=NULL.
 */
typedef struct {
  char *label;              /* The MenuItem label text */
  XtCallbackProc callback;  /* Function to be called when field is selected */
} MenuItem;

static Widget CreateOptionMenu(char *name, char *label, Widget parent, int nopt,
			       MenuItem *opts, XtPointer client_data);
static Widget CreatePulldownMenu(char *name, char *label, Widget parent,
				 int nfield, MenuItem *fields,
				 XtPointer client_data);
static void PopulateMainMenuBar( Image *im, Widget w_bar );
void centerWidgetAndMouse(Widget widget, Widget pwidget, Widget mwidget);
static Widget XawDialogGetText( Widget dialog );
static Widget GetTopShell( Widget w );
static CALL_FN(set_label_callback);
static Widget CreatePopupPromptDialog(Widget w, char *name, char *prompt,
				      char *value,
				      XtCallbackProc ok_fn, XtPointer ok_data);

static void start_slice_callback(Widget w, XtPointer client_data,
				 XtPointer call_data);
static void end_slice_callback(Widget w, XtPointer client_data,
			       XtPointer call_data);
static CALL_FN(quit_callback);
static CALL_FN(help_callback);
static CALL_FN(save_image_as_callback);
static CALL_FN(save_image_callback);
static CALL_FN(destroy_widget_callback);

static void report_cursor(Widget w, XtPointer context, XEvent *event,
			  Boolean *call_next);

/*.......................................................................
 * A demo program showing an example of how to use PGPLOT with Athena.
 *
 * Output:
 *  return   int  0 - OK.
 *                1 - Error.
 */
int
main(int argc, char *argv[])
{
	XtAppContext app;/* Application context returned by XtVaAppInitialize */
	Widget w_top;    /* The top-level widget of the application */
	Widget w_main;   /* The geometry management widget of the application */
	Widget w_bar;    /* The menubar of the application */
	Image *im;       /* Image object container */
	/*
	 * Initialize Xt.
	 */
	XtSetLanguageProc(NULL, NULL, NULL);
	w_top = XtVaAppInitialize(&app, "ImageSlice", NULL, 0,
#if XtSpecificationRelease <= 4
				  (Cardinal *)
#endif
				  &argc, argv, NULL,
				  NULL);
	w_main = XtVaCreateWidget("main",
				  panedWidgetClass,
				  w_top,
				  XtNheight, 760,
				  XtNwidth, 420,
				  NULL);
	/*
	 * Create a placeholder for the menubar
	 */
	w_bar = XtVaCreateWidget("menuBar",
				 formWidgetClass,
				 w_main,
				 XtNshowGrip, False,
				 XtNskipAdjust, True,
				 NULL);
	/*
	 * Create the two PGPLOT widgets and the image container object.
	 */
	im = new_Image(MAP_SIZE, SLICE_SIZE, ring_fn, w_main, w_top);
	if (!im)
		return 1;
	/*
	 * Create the application menu-bar.
	 */
	PopulateMainMenuBar(im, w_bar);
	XtManageChild(w_bar);
	XtManageChild(w_main);

	/*
	 * Display the widgets.
	 */
	XtRealizeWidget(w_top);
	/*
	 * Open the widgets to PGPLOT.
	 */
	if (cpgopen(xap_device_name(im->w_image)) <= 0 ||
	    cpgopen(xap_device_name(im->w_slice)) <= 0)
		return 1;
	/*
	 * Display the initial image.
	 */
	display_fn(im, ring_fn);
	/*
	 * Interact with the user.
	 */
	XtAppMainLoop(app);
	return 0;
}

/*.......................................................................
 * Allocate and return an initialized Image container object.
 * This function creates two PGPLOT widgets. One will be used to display
 * a gray-scale image. The other will be used to display a slice through
 * the image.
 *
 * Note that the widgets are not opened to PGPLOT and nothing
 * will be displayed until display_fn() is first
 * called. These operations must be postponed until after the widgets have
 * been realized.
 *
 * Input:
 *  image_size unsigned    The number of pixels along each side of the
 *                         square image array. This must be an odd
 *                         number (so that there can be a central pixel).
 *  slice_size unsigned    The dimension of the slice array (>=2).
 *  fn         IMAGE_FN(*) The initial display function.
 *  parent       Widget    The widget in which to create the PGPLOT widgets.
 *  w_top        Widget    The top-level widget of the application.
 * Output:
 *  return   Image *  The new image container, or NULL on error.
 */
static Image *new_Image(unsigned image_size, unsigned slice_size,
			IMAGE_FN(*fn), Widget parent, Widget w_top)
{
  Image *im;       /* The pointer to the container to be returned */
  Widget w_frame;  /* A frame widget */
  int i;
/*
 * Check the arguments.
 */
  if(image_size < 1 || image_size % 2 == 0) {
    fprintf(stderr, "new_Image: Illegal image size requested.\n");
    return NULL;
  };
  if(slice_size < 2) {
    fprintf(stderr, "new_Image: Illegal slice size requested.\n");
    return NULL;
  };
  if(!fn) {
    fprintf(stderr, "new_Image: NULL display function intercepted.\n");
    return NULL;
  };
/*
 * Allocate the container.
 */
  im = (Image *) malloc(sizeof(Image));
  if(!im) {
    fprintf(stderr, "new_Image: Insufficient memory.\n");
    return NULL;
  };
/*
 * Before attempting any operation that might fail, initialize the
 * Image container at least up to the point at which it can safely be
 * passed to del_Image().
 */
  im->w_coord = NULL;
  im->w_image = NULL;
  im->w_slice = NULL;
  im->image = NULL;
  im->slice = NULL;
  im->image_size = image_size;
  im->slice_size = slice_size;
  im->scale = 40.0f/image_size;
  im->xa = -(int)image_size/2;
  im->xb = image_size/2;
  im->ya = -(int)image_size/2;
  im->yb = image_size/2;
  im->fn = fn;
  im->busy = None;
  im->w_top = w_top;
/*
 * Now allocate the 2D image array as a 1D array to be indexed in
 * as a FORTRAN array.
 */
  im->image = (float *) malloc(sizeof(float) * image_size * image_size);
  if(!im->image) {
    fprintf(stderr, "new_Image: Insufficient memory.\n");
    return del_Image(im);
  };
/*
 * Initialize the image array.
 */
  for(i=0; i<image_size*image_size; i++)
    im->image[i] = 0.0f;
/*
 * Allocate an array to be used when constructing slices through the
 * displayed image.
 */
  im->slice = (float *) malloc(sizeof(float) * slice_size);
  if(!im->slice) {
    fprintf(stderr, "new_Image: Insufficient memory.\n");
    return del_Image(im);
  };
/*
 * Initialize the slice array.
 */
  for(i=0; i<slice_size; i++)
    im->slice[i] = 0.0f;
/*
 * Create a horizontal row-column widget in which to arrange the
 * coordinate-display labels.
 */
  w_frame = XtVaCreateManagedWidget("coord_form", formWidgetClass, parent,
				    NULL);
/*
 * Create two labels. The first will contain a prefix, and the second
 * will contain the coordinates.
 */
  {
    char *text = "World coordinates: ";
    Widget w_clab = XtVaCreateManagedWidget("clab",
					    labelWidgetClass, w_frame,
					    XtNlabel, text,
					    XtNleft, XawChainLeft,
					    XtNright, XawChainLeft,
					    NULL);
    im->w_coord = XtVaCreateManagedWidget("coord",
					labelWidgetClass, w_frame,
					XtNlabel, "",
					XtNfromHoriz, w_clab,
					XtNleft, XawChainLeft,
					XtNright, XawChainRight,
					NULL);
  };
/*
 * Create an etched-in frame widget to provide a border for the
 * image window.
 */
  w_frame = XtVaCreateManagedWidget("image_frame",
				    viewportWidgetClass,
				    parent,
				    XtNallowHoriz, True,
				    XtNallowVert, True,
				    NULL);
/*
 * Create the image-display widget.
 */
  im->w_image = XtVaCreateManagedWidget("image", xaPgplotWidgetClass,
					w_frame,
					XtNheight, 400,
					XtNwidth, 400,
					XapNmaxColors, 50,
					XapNshare, True,
					NULL);
/*
 * Register a motion-event callback such that the cursor position can
 * be reported in the im->w_coord label widget.
 */
  XtAddEventHandler(im->w_image, PointerMotionMask, False, report_cursor,
		    (XtPointer)im->w_coord);
/*
 * Create a pulldown menu of optional 2-D image functions.
 */
  {
    static MenuItem functions[] = {
      {"Display Functions", NULL},  /* Title */
      {NULL, NULL},                 /* Separator */
      {"R=Polar radius", NULL},     /* Label */
      {"A=Polar angle", NULL},      /* Label */
      {NULL, NULL},                 /* Separator */
      {"cos(R)sin(A)",                 ring_callback},
      {"sinc(R)",                      sinc_callback},
      {"exp(-R^2/20.0)",               gaus_callback},
      {"sin(A)",                       sin_angle_callback},
      {"cos(R)",                       cos_radius_callback},
      {"(1+sin(6A))exp(-R^2/100)",     star_callback},
    };
    Widget menu = CreateOptionMenu("functions", "Select a display function:",
				   parent,
				   sizeof(functions)/sizeof(functions[0]),
				   functions, (XtPointer) im);
    if(menu == NULL)
      return del_Image(im);
    XtManageChild(menu);
  };
/*
 * Create a pulldown menu of optional color tables.
 */
  {
    static MenuItem tables[] = {
      {"Color Tables", NULL},  /* Title */
      {NULL, NULL},            /* Separator */
      {"grey",                 grey_callback},
      {"rainbow",              rainbow_callback},
      {"heat",                 heat_callback},
      {"aips",                 aips_callback},
    };
    Widget menu = CreateOptionMenu("Colors", "Select a color table:",
				   parent,
				   sizeof(tables)/sizeof(tables[0]),
				   tables, (XtPointer) im);
    if(menu == NULL)
      return del_Image(im);
    XtManageChild(menu);
  };
/*
 * Create an etched-in frame widget to provide a border for the
 * slice-plot window.
 */
  w_frame = XtVaCreateManagedWidget("image_frame",
				    viewportWidgetClass,
				    parent,
				    XtNallowHoriz, True,
				    XtNallowVert, True,
				    NULL);
/*
 * Create the slice-display widget.
 */
  im->w_slice = XtVaCreateManagedWidget("slice", xaPgplotWidgetClass,
					w_frame,
					XtNheight, 200,
					XtNwidth, 400,
					XapNmaxColors, 16,
					XapNshare, True,
					NULL);
/*
 * Get the standard X busy cursor.
 */
  im->busy = XCreateFontCursor(XtDisplay(im->w_top), XC_watch);
  return im;
}

/*.......................................................................
 * Delete an Image container previously returned by new_Image().
 *
 * Input:
 *  im        Image *  The container to be deleted (or NULL).
 * Output:
 *  return    Image *  The deleted container (always NULL).
 */
Image *del_Image(Image *im)
{
  if(im) {
    if(im->image)
      free(im->image);
    if(im->w_coord)
      XtDestroyWidget(im->w_coord);
    if(im->w_image)
      XtDestroyWidget(im->w_image);
    if(im->w_slice)
      XtDestroyWidget(im->w_slice);
    if(im->busy != None)
      XFreeCursor(XtDisplay(im->w_top), im->busy);
    free(im);
  };
  return NULL;
}

/*.......................................................................
 * Display a new function in the image window.
 *
 * Input:
 *  im    Image *   The image context object.
 *  fn IMAGE_FN(*)  The function to be displayed.
 */
static void display_fn(Image *im, IMAGE_FN(*fn))
{
  int ix, iy;  /* The pixel coordinates being assigned */
  float vmin;  /* The minimum pixel value in the image */
  float vmax;  /* The maximum pixel value in the image */
  float *pixel;/* A pointer to pixel (ix,iy) in im->image */
/*
 * Check arguments.
 */
  if(!fn) {
    fprintf(stderr, "display_fn: NULL function.\n");
    return;
  };
/*
 * Disarm the cursor while the image-plot is incomplete.
 */
  xap_disarm_cursor(im->w_image);
/*
 * Install the new function.
 */
  im->fn = fn;
/*
 * Fill the image array via the current display function.
 */
  pixel = im->image;
  vmin = vmax = im->fn(im->xa * im->scale, im->ya * im->scale);
  for(iy = im->ya; iy <= im->yb; iy++) {
    for(ix = im->xa; ix <= im->xb; ix++) {
      float value = im->fn(ix * im->scale, iy * im->scale);
      *pixel++ = value;
      if(value < vmin)
	vmin = value;
      if(value > vmax)
	vmax = value;
    };
  };
/*
 * Record the min and max values of the data array.
 */
  im->datamin = vmin;
  im->datamax = vmax;
/*
 * Display the new image.
 */
  display_image(im, xap_device_id(im->w_image));
/*
 * Arm the cursor for user selection of the start position of the
 * first slice line through this image.
 */
  xap_arm_cursor(im->w_image, XAP_NORM_CURSOR, 0.0f, 0.0f,
		 start_slice_callback, im);
/*
 * Display instructions in the slice window.
 */
  display_help(im);
  return;
}

/*.......................................................................
 * Display the current image function in a specified PGPLOT device.
 *
 *
 * Input:
 *  im    Image *   The image context object.
 *  id      int     The id of the PGPLOT device to display.
 */
static void display_image(Image *im, int id)
{
  int minind,maxind;  /* The range of available color indexes */
  float tr[6];        /* Image coordinate-transformation matrix */
/*
 * Since rendering a gray-scale image takes a few seconds
 * display the busy cursor.
 */
  XDefineCursor(XtDisplay(im->w_top), XtWindow(im->w_top), im->busy);
  XFlush(XtDisplay(im->w_top));
/*
 * Select the specified PGPLOT device and display the image array.
 */
  cpgslct(id);
  cpgask(0);
  cpgpage();
  cpgsch(1.0f);
  cpgvstd();
  cpgwnad(im->xa * im->scale, im->xb * im->scale,
	  im->ya * im->scale, im->yb * im->scale);
/*
 * Set up the pixel -> world coordinate transformation matrix.
 */
  tr[0] = (im->xa - 1) * im->scale;
  tr[1] = im->scale;
  tr[2] = 0.0f;
  tr[3] = (im->ya - 1) * im->scale;
  tr[4] = 0.0f;
  tr[5] = im->scale;
/*
 * If there are fewer than 2 colors available for plotting images,
 * mark the image as monochrome so that pggray can be asked to
 * produce a stipple version of the image.
 */
  cpgqcir(&minind, &maxind);
  if(maxind-minind+1 <= 2) {
    cpggray(im->image, im->image_size, im->image_size,
	    1, im->image_size, 1, im->image_size, im->datamax, im->datamin, tr);
  } else {
    cpgimag(im->image, im->image_size, im->image_size,
	    1, im->image_size, 1, im->image_size, im->datamin, im->datamax, tr);
  };
  cpgsci(1);
  cpgbox("BCNST", 0.0f, 0, "BCNST", 0.0f, 0);
  cpglab("X", "Y", "Image display demo");
/*
 * Revert to the normal X cursor.
 */
  XDefineCursor(XtDisplay(im->w_top), XtWindow(im->w_top), None);
  return;
}

/*.......................................................................
 * Display a new slice in the slice window.
 *
 * Input:
 *  im    Image *   The image context object.
 *  va   Vertex *   The vertex of one end of the slice line.
 *  vb   Vertex *   The vertex of the opposite end of the slice line.
 */
static void display_slice(Image *im, Vertex *va, Vertex *vb)
{
  float xa;  /* The start X value of the slice */
  float dx;  /* The X-axis world-coordinate width of one slice pixel */
  float ya;  /* The start Y value of the slice */
  float dy;  /* The Y-axis world-coordinate width of one slice pixel */
  float smin = HUGE ;/* The minimum slice value */
  float smax = -HUGE;/* The maximum slice value */
  float slice_length;  /* The world-coordinate length of the slice */
  float ymargin;       /* The Y axis margin within the plot */
  int i;
/*
 * Determine the slice pixel assignments.
 */
  xa = va->x;
  dx = (vb->x - va->x) / im->slice_size;
  ya = va->y;
  dy = (vb->y - va->y) / im->slice_size;
/*
 * Make sure that the slice has a finite length by setting a
 * minimum size of one pixel.
 */
  {
    float min_delta = im->scale / im->slice_size;
    if(fabs(dx) < min_delta && fabs(dy) < min_delta)
      dx = min_delta;
  };
/*
 * Construct the slice in im->slice[] and keep a tally of the
 * range of slice values seen.
 */
  for(i=0; i<im->slice_size; i++) {
    float value = im->fn(xa + i * dx, ya + i * dy);
    im->slice[i] = value;
    if(i==0) {
      smin = smax = value;
    } else if(value < smin) {
      smin = value;
    } else if(value > smax) {
      smax = value;
    };
  };
/*
 * Determine the length of the slice.
 */
  {
    float xlen = dx * im->slice_size;
    float ylen = dy * im->slice_size;
    slice_length = sqrt(xlen * xlen + ylen * ylen);
  };
/*
 * Determine the extra length to add to the Y axis to prevent the
 * slice plot hitting the top and bottom of the plot.
 */
  ymargin = 0.05 * (im->datamax - im->datamin);
/*
 * Set up the slice axes.
 */
  cpgslct(xap_device_id(im->w_slice));
  cpgask(0);
  cpgpage();
  cpgbbuf();
  cpgsch(2.0f);
  cpgvstd();
  cpgswin(0.0f, slice_length, im->datamin - ymargin, im->datamax + ymargin);
  cpgbox("BCNST", 0.0f, 0, "BCNST", 0.0f, 0);
  cpglab("Radius", "Image value", "A 1D slice through the image");
/*
 * Draw the slice.
 */
  for(i=0; i<im->slice_size; i++) {
    if(i==0)
      cpgmove(0.0f, im->slice[0]);
    else
      cpgdraw(slice_length * (float)i / (float)im->slice_size, im->slice[i]);
  };
  cpgebuf();
  return;
}

/*.......................................................................
 * Display usage instructions in the slice window.
 *
 * Input:
 *  im     Image *   The image object.
 */
static void display_help(Image *im)
{
/*
 * Clear the slice plot and replace it with instructional text.
 */
  cpgslct(xap_device_id(im->w_slice));
  cpgask(0);
  cpgpage();
  cpgsch(3.5f);
  cpgsvp(0.0, 1.0, 0.0, 1.0);
  cpgswin(0.0, 1.0, 0.0, 1.0);
  cpgmtxt("T", -2.0, 0.5, 0.5,
	  "To see a slice through the image, move the");
  cpgmtxt("T", -3.0, 0.5, 0.5,
	  "mouse into the image display window and select");
  cpgmtxt("T", -4.0, 0.5, 0.5,
	  " the two end points of a line.");
}

/*.......................................................................
 * A sinc(radius) function.
 *
 * Input:
 *  x,y     float   The coordinates to evaluate the function at.
 * Output:
 *  return  float   The function value at the specified coordinates.
 */
static IMAGE_FN(sinc_fn)
{
  const float tiny = 1.0e-6f;
  float radius = sqrt(x*x + y*y);
  return (fabs(radius) < tiny) ? 1.0f : sin(radius)/radius;
}

/*.......................................................................
 * Callback to select the sinc_fn() fucntion.
 */
static CALL_FN(sinc_callback)
{
  display_fn((Image *) client_data, sinc_fn);
}

/*.......................................................................
 * A exp(-(x^2+y^2)/20) function.
 *
 * Input:
 *  x,y     float   The coordinates to evaluate the function at.
 * Output:
 *  return  float   The function value at the specified coordinates.
 */
static IMAGE_FN(gaus_fn)
{
  return exp(-((x*x)+(y*y))/20.0f);
}

/*.......................................................................
 * Callback to select the gaus_fn() fucntion.
 */
static CALL_FN(gaus_callback)
{
  display_fn((Image *) client_data, gaus_fn);
}

/*.......................................................................
 * A cos(radius)*sin(angle) function.
 *
 * Input:
 *  x,y     float   The coordinates to evaluate the function at.
 * Output:
 *  return  float   The function value at the specified coordinates.
 */
static IMAGE_FN(ring_fn)
{
  return cos(sqrt(x*x + y*y)) * sin(x==0.0f && y==0.0f ? 0.0f : atan2(x,y));
}

/*.......................................................................
 * Callback to select the ring_fn() fucntion.
 */
static CALL_FN(ring_callback)
{
  display_fn((Image *) client_data, ring_fn);
}

/*.......................................................................
 * A sin(angle) function.
 *
 * Input:
 *  x,y     float   The coordinates to evaluate the function at.
 * Output:
 *  return  float   The function value at the specified coordinates.
 */
static IMAGE_FN(sin_angle_fn)
{
  return sin(x==0.0f && y==0.0f ? 0.0f : atan2(x,y));
}

/*.......................................................................
 * Callback to select the sin_angle_fn() fucntion.
 */
static CALL_FN(sin_angle_callback)
{
  display_fn((Image *) client_data, sin_angle_fn);
}

/*.......................................................................
 * A cos(radius) function.
 *
 * Input:
 *  x,y     float   The coordinates to evaluate the function at.
 * Output:
 *  return  float   The function value at the specified coordinates.
 */
static IMAGE_FN(cos_radius_fn)
{
  return cos(sqrt(x*x + y*y));
}

/*.......................................................................
 * Callback to select the cos_radius_fn() fucntion.
 */
static CALL_FN(cos_radius_callback)
{
  display_fn((Image *) client_data, cos_radius_fn);
}

/*.......................................................................
 * A (1+sin(6*angle))*exp(-radius^2 / 100)function.
 *
 * Input:
 *  x,y     float   The coordinates to evaluate the function at.
 * Output:
 *  return  float   The function value at the specified coordinates.
 */
static IMAGE_FN(star_fn)
{
  return (1.0 + sin(x==0.0f && y==0.0f ? 0.0f : 6.0*atan2(x,y)))
    * exp(-((x*x)+(y*y))/100.0f);
}

/*.......................................................................
 * Callback to select the star_fn() fucntion.
 */
static CALL_FN(star_callback)
{
  display_fn((Image *) client_data, star_fn);
}

/*.......................................................................
 * Create an option menu.
 *
 * Input:
 *  name             char *  The name of the menu.
 *  label            char *  The instructive label to place to the left of
 *                           the option menu.
 *  parent         Widget    The widget in which to place the option menu.
 *  nopt              int    The number of option fields.
 *  opts         MenuItem *  An array of nopt option fields.
 *  client_data XtPointer    The client_data argument to be passed to each
 *                           callback function.
 * Output:
 *  return      Widget    The menu, or NULL on error.
 */
static Widget
CreatePulldownMenu( char *name, char *label, Widget parent,
		    int nopt, MenuItem *opts, XtPointer client_data )
{
	Widget w_menu;       /* The option menu to be returned */
	Widget w_pulldown;   /* The pulldown-menu of the option menu widget */
	int i;
	static char buf[100];
	char* menuName;
	/*
	 * Check arguments.
	 */
	if (nopt < 1 || !opts) {
		fprintf(stderr, "CreateOptionMenu: No options.\n");
		return NULL;
	};
	sprintf(buf, "%sMenu", name); /* Menuname */
	menuName = XtNewString(buf);
	
	/*
	 * Create a menuButton
	 */
	w_menu = XtVaCreateManagedWidget(name,
					 menuButtonWidgetClass,
					 parent,
					 XtNlabel, label,
					 XtNmenuName, menuName,
					 NULL);
	/*
	 * Create a pulldown menu.
	 */
	w_pulldown = XtVaCreatePopupShell(menuName,
					  simpleMenuWidgetClass,
					  w_menu,
					  NULL);
	/*
	 * Install the option fields.
	 */
	for (i=0; i<nopt; i++) {
		MenuItem *opt = opts + i;
		if (opt->label) {
			/*
			 * Add an option field.
			 */
			if (opt->callback) {
				Widget widget = XtVaCreateManagedWidget(opt->label,
									smeBSBObjectClass,
									w_pulldown, NULL);
				XtAddCallback(widget, XtNcallback, opt->callback, client_data);
			} else {
				/*
				 * Add a title widget.
				 */
				XtVaCreateManagedWidget(opt->label,
							smeBSBObjectClass,
							w_pulldown, NULL);
			};
		} else {
			/*
			 * Add a separator widget.
			 */
			XtVaCreateManagedWidget("separator",
						smeLineObjectClass,
						w_pulldown, NULL);
		};
	};
	return w_menu;
}

static Widget
CreateOptionMenu( char *name, char *label, Widget parent,
		  int nopt, MenuItem *opts, XtPointer client_data )
{
	Widget w_form;       /* The container widget for the option menu */
	Widget w_label;      /* Label for the line */
	Widget w_menu;       /* The option menu to be returned */
	Widget w_pulldown;   /* The pulldown-menu of the option menu widget */
	int i;
	static char buf[100];
	char* menuName;
	Boolean first = True;
	
	/*
	 * Check arguments.
	 */
	if (nopt < 1 || !opts) {
		fprintf(stderr, "CreateOptionMenu: No options.\n");
		return NULL;
	};
	sprintf(buf, "%sMenu", name); /* Menuname */
	menuName = XtNewString(buf);
	
	/*
	 * Create container
	 */
	w_form = XtVaCreateManagedWidget(name,
					 formWidgetClass,
					 parent,
					 XtNskipAdjust, True,
					 XtNshowGrip, False,
					 NULL);
	/*
	 * Create a label
	 */
	w_label = XtVaCreateManagedWidget(label,
					  labelWidgetClass,
					  w_form,
					  XtNleft, XawChainLeft,
					  XtNright, XawChainLeft,
					  NULL);
	/*
	 * Create a menuButton
	 */
	w_menu = XtVaCreateManagedWidget(name,
					 menuButtonWidgetClass,
					 w_form,
					 XtNmenuName, menuName,
					 XtNfromHoriz, w_label,
					 XtNleft, XawChainLeft,
					 XtNright, XawChainLeft,
					 XtNresizable, True,
					 NULL);
	/*
	 * Create a pulldown menu.
	 */
	w_pulldown = XtVaCreatePopupShell(menuName,
					  simpleMenuWidgetClass,
					  w_menu,
					  NULL);
	/*
	 * Install the option fields.
	 */
	for (i=0; i<nopt; i++) {
		MenuItem *opt = opts + i;
		if (opt->label) {
			/* 
			 * Set label of Menubutton
			 */
			if (first) {
				first = False;
				XtVaSetValues(w_menu,
					      XtNlabel, opt->label,
					      NULL);
			}
			/*
			 * Add an option field.
			 */
			if (opt->callback) {
				Widget widget = XtVaCreateManagedWidget(opt->label,
									smeBSBObjectClass,
									w_pulldown, NULL);
				XtAddCallback(widget, XtNcallback, opt->callback, client_data);
				XtAddCallback(widget, XtNcallback, set_label_callback, (XtPointer)w_menu);
			} else {
				/*
				 * Add a title widget.
				 */
				XtVaCreateManagedWidget(opt->label,
							smeBSBObjectClass,
							w_pulldown, NULL);
			};
		} else {
			/*
			 * Add a separator widget.
			 */
			XtVaCreateManagedWidget("separator",
						smeLineObjectClass,
						w_pulldown, NULL);
		};
	};
	return w_menu;
}

static void
set_label_callback( Widget w, XtPointer client_data, XtPointer call_data )
{
	Widget w_menu = (Widget)client_data;
	char* label;
	XtVaGetValues(w, XtNlabel, &label, NULL);
	XtVaSetValues(w_menu, XtNlabel, label, NULL);
}
	
/*.......................................................................
 * This callback is called when the user selects the start position
 * of a slice line.
 *
 * Input:
 *  Widget          widget The PGPLOT widget that had a cursor event.
 *  client_data  XtPointer The optional client data pointer passed to
 *                         xap_arm_cursor().
 *  call_data    XtPointer The pointer to the context of the event
 *                         as a (XapCursorCallbackStruct *) cast to
 *                         (XtPointer).
 */
static void start_slice_callback(Widget w, XtPointer client_data,
				 XtPointer call_data)
{
  XapCursorCallbackStruct *cursor = (XapCursorCallbackStruct *) call_data;
  Image *im = (Image *) client_data;
  im->va.x = cursor->x;
  im->va.y = cursor->y;
/*
 * Display a line-oriented rubber-band cursor to get the end vertex of the
 * line.
 */
  cpgslct(xap_device_id(im->w_image));
  cpgsci(3);
  xap_arm_cursor(im->w_image, XAP_LINE_CURSOR, im->va.x, im->va.y,
		 end_slice_callback, im); 
}

/*.......................................................................
 * This callback is called when the user selects the end position
 * of a slice line.
 *
 * Input:
 *  Widget          widget The PGPLOT widget that had a cursor event.
 *  client_data  XtPointer The optional client data pointer passed to
 *                         xap_arm_cursor().
 *  call_data    XtPointer The pointer to the context of the event
 *                         as a (XapCursorCallbackStruct *) cast to
 *                         (XtPointer).
 */
static void end_slice_callback(Widget w, XtPointer client_data,
			       XtPointer call_data)
{
  XapCursorCallbackStruct *cursor = (XapCursorCallbackStruct *) call_data;
  Image *im = (Image *) client_data;
  im->vb.x = cursor->x;
  im->vb.y = cursor->y;
/*
 * Re-arm the cursor for the start of the next line.
 */
  xap_arm_cursor(im->w_image, XAP_NORM_CURSOR, 0.0f, 0.0f,
		 start_slice_callback, im);
/*
 * Draw the slice wrt the new line.
 */
  display_slice(im, &im->va, &im->vb);
}

/*.......................................................................
 * Create the menu bar of the application.
 *
 * Input:
 *  im        Image * The image object of the application.
 *  w_main   Widget   The form widget for the menubar
 */
static void
PopulateMainMenuBar( Image *im, Widget w_bar )
{
	Widget file;         /* The file menu-button */
	
	/*
	 * Install the File menu.
	 */
	{
		static MenuItem file_fields[] = {
			{"Save image as", save_image_as_callback},
			{NULL, NULL},     /* Separator */
			{"Quit",          quit_callback}
		};
		file = CreatePulldownMenu("file_menu", 
					  "File",
					  w_bar,
					  XtNumber(file_fields),
					  file_fields,
					  (XtPointer) im);
		XtVaSetValues(file,
			      XtNleft, XawChainLeft,
			      XtNright, XawChainLeft,
			      NULL);
	};
	/*
	 * Install the Help menu.
	 */
	{
		static MenuItem help_fields[] = {
			{"Usage", help_callback}
		};
		Widget w_help = CreatePulldownMenu("help_menu",
						   "Help",
						   w_bar,
						   XtNumber(help_fields),
						   help_fields,
						   (XtPointer) im);
		XtVaSetValues(w_help,
			      XtNleft, XawChainRight,
			      XtNright, XawChainRight,
			      XtNfromHoriz, file,
			      NULL);
	};
}

/*.......................................................................
 * The file-menu "Quit" callback function.
 */
static CALL_FN(quit_callback)
{
  exit(0);
}

/*.......................................................................
 * The help-menu callback function.
 */
static CALL_FN(help_callback)
{
  Image *im = (Image *) client_data;
  display_help(im);
}

/*.......................................................................
 * The File-menu "save image as" callback.
 */
static CALL_FN(save_image_as_callback)
{
  Image *im = (Image *) client_data;
  Widget w_dialog = CreatePopupPromptDialog(w, "device",
                          "Enter a PGPLOT device string:",
			  "image.ps/vps                 ",
			  save_image_callback, (XtPointer) im);
/*
 * Add a null translation for the letter '?'. This prevents users from
 * enterring a PGPLOT '?' query string.
 */
  {
    char *bindings ="s <Key>\?:";
    XtTranslations translations = XtParseTranslationTable(bindings);
    XtOverrideTranslations(XawDialogGetText(w_dialog), translations);
  };
/*
 * Display the dialog.
 */
  XtManageChild(w_dialog);
  XtPopup(XtParent(w_dialog), XtGrabNone);
}

/*.......................................................................
 * The callback for the dialog created by save_image_as_callback().
 */
static
CALL_FN(save_image_callback)
{
	int device_id;  /* The PGPLOT ID of the new PGPLOT device */
	Image *im = (Image *) client_data;
	Widget dialog = XtParent(w);
	
	/*
	 * Get device specification.
	 */
	{
		char *device = XawDialogGetValueString(dialog);
		if( device ) {
			/*
			 * Open the specified device.
			 */
			device_id = cpgopen(device);
			if (device_id > 0) {
				display_image(im, device_id);
				cpgclos();
			};
		}
	};
	/*
	 * Discard the popup widget.
	 */
	XtDestroyWidget(GetTopShell(w));
	return;
}

/*.......................................................................
 * Create a popup prompt-dialog with a specified prompt and initial value.
 *
 * Input:
 *  w             Widget    The widget of the button that invoked the dialog.
 *  name            char *  The name to give the popup. Note that
 *                          XaCreatePromptDialog() appends _prompt to this.
 *  prompt          char *  The dialog prompt string.
 *  value           char *  The initial value to display, or NULL.
 *  ok_fn XtCallbackProc    The callback function for the OK button.
 *  ok_data    XtPointer    The callback client_data argument.
 * Output:
 *  return  Widget     The dialog widget.
 */
static Widget
CreatePopupPromptDialog( Widget w, char *name, char *prompt,
			 char *value,
			 XtCallbackProc ok_fn, XtPointer ok_data )
{
	Widget w_dialog;  /* The dialog widget to be returned */
	Widget shell, top, w_text;
	
	top = GetTopShell(w);
	
	/*
	 * Create the dialog.
	 */
	{
		char buf[100];
		sprintf(buf, "%sShell", name);
		shell = XtVaCreatePopupShell(buf,
					     transientShellWidgetClass,
					     top,
					     NULL);
		w_dialog = XtVaCreateManagedWidget(name,
						   dialogWidgetClass,
						   shell,
						   XtNlabel, prompt,
						   XtNvalue, value ? value : "",
						   NULL);
		w_text = XawDialogGetText(w_dialog);
		{
			char* trans = "#override\n<KeyPress>Return: set() notify()\n<KeyRelease>Return: unset()";
			XtAccelerators accel = XtParseAcceleratorTable(trans);
			Widget w_button = XtVaCreateManagedWidget("Ok",
								  commandWidgetClass,
								  w_dialog,
								  XtNaccelerators, accel,
								  NULL);
			XtAddCallback(w_button, XtNcallback, ok_fn, ok_data);
			XtInstallAccelerators(w_text, w_button);
			XtInstallAccelerators(w_dialog, w_button);
		}
			
		XawDialogAddButton(w_dialog, "Cancel", destroy_widget_callback, shell);
		XtVaSetValues(w_text,
			      XtNright, XawChainRight,
			      XtNleft, XawChainLeft,
			      NULL);
	};
	XtRealizeWidget(shell);
	centerWidgetAndMouse(shell, top, top);
	return w_dialog;
}

/*.......................................................................
 * A callback that destroys its client_data.
 */
static
CALL_FN(destroy_widget_callback)
{
	XtDestroyWidget( (Widget)client_data );
}

static Widget
XawDialogGetText( Widget dialog )
{
	return XtNameToWidget(dialog, "value");
}

static Widget
GetTopShell( Widget w )
{
	while (w && !XtIsWMShell(w))
		w = XtParent(w);
	return w;
}

/*.......................................................................
 * This is a motion-event callback for the image window. It reports the
 * current position of the cursor in world coordinates.
 *
 * Input:
 *  w           Widget   The im->w_image PGPLOT widget.
 *  context  XtPointer   The im->w_coord label widget cast to XtPointer.
 *  event       XEvent * The motion event.
 *  call_next  Boolean * *call_next will be left as True so that any
 *                       following event handlers will get called.
 */
static void report_cursor(Widget w, XtPointer context, XEvent *event,
			  Boolean *call_next)
{
  Widget w_coord = (Widget) context;
  if(event->type == MotionNotify) {
    char text[80];
    float wx, wy;
/*
 * Convert from X-window coordinates to world coordinates.
 */
    if(xap_pixel_to_world(w, event->xmotion.x, event->xmotion.y,
			  &wx, &wy) == 0) {
      sprintf(text, "X=%-10g Y=%-10g", wx, wy);
      XtVaSetValues(w_coord,
		    XtNlabel, text,
		    NULL);
    };
  };
  *call_next = True;
}

/*.......................................................................
 * Callback to select a grey colormap fucntion.
 */
static CALL_FN(grey_callback)
{
  static float grey_l[] = {0.0, 1.0};
  static float grey_c[] = {0.0, 1.0};
  recolor_image((Image *) client_data, grey_l, grey_c, grey_c, grey_c,
		sizeof(grey_l)/sizeof(grey_l[0]));
}

/*.......................................................................
 * Callback to select a rainbow colormap fucntion.
 */
static CALL_FN(rainbow_callback)
{
  static float rain_l[] = {-0.5, 0.0, 0.17, 0.33, 0.50, 0.67, 0.83, 1.0, 1.7};
  static float rain_r[] = { 0.0, 0.0,  0.0,  0.0,  0.6,  1.0,  1.0, 1.0, 1.0};
  static float rain_g[] = { 0.0, 0.0,  0.0,  1.0,  1.0,  1.0,  0.6, 0.0, 1.0};
  static float rain_b[] = { 0.0, 0.3,  0.8,  1.0,  0.3,  0.0,  0.0, 0.0, 1.0};
  recolor_image((Image *) client_data, rain_l, rain_r, rain_g, rain_b,
		sizeof(rain_l)/sizeof(rain_l[0]));
}

/*.......................................................................
 * Callback to select the IRAF "heat" colormap fucntion.
 */
static CALL_FN(heat_callback)
{
  static float heat_l[] = {0.0, 0.2, 0.4, 0.6, 1.0};
  static float heat_r[] = {0.0, 0.5, 1.0, 1.0, 1.0};
  static float heat_g[] = {0.0, 0.0, 0.5, 1.0, 1.0};
  static float heat_b[] = {0.0, 0.0, 0.0, 0.3, 1.0};
  recolor_image((Image *) client_data, heat_l, heat_r, heat_g, heat_b,
		sizeof(heat_l)/sizeof(heat_l[0]));
}

/*.......................................................................
 * Callback to select the aips tvfiddle colormap fucntion.
 */
static CALL_FN(aips_callback)
{
  static float aips_l[] = {0.0, 0.1, 0.1, 0.2, 0.2, 0.3, 0.3, 0.4, 0.4, 0.5,
			     0.5, 0.6, 0.6, 0.7, 0.7, 0.8, 0.8, 0.9, 0.9, 1.0};
  static float aips_r[] = {0.0, 0.0, 0.3, 0.3, 0.5, 0.5, 0.0, 0.0, 0.0, 0.0,
			     0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
  static float aips_g[] = {0.0, 0.0, 0.3, 0.3, 0.0, 0.0, 0.0, 0.0, 0.8, 0.8,
			     0.6, 0.6, 1.0, 1.0, 1.0, 1.0, 0.8, 0.8, 0.0, 0.0};
  static float aips_b[] = {0.0, 0.0, 0.3, 0.3, 0.7, 0.7, 0.7, 0.7, 0.9, 0.9,
			     0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  recolor_image((Image *) client_data, aips_l, aips_r, aips_g, aips_b,
		sizeof(aips_l)/sizeof(aips_l[0]));
}

/*.......................................................................
 * Change the colors used to display the current image.
 *
 * Inputs:
 *  im     Image *  The image widget resource object.
 *  lev    float *  The array of n normalized brightness levels at which
 *                  red,green and blue levels are to be defined.
 *  r      float *  The red brightness at each of the levels in lev[].
 *  g      float *  The green brightness at each of the levels in lev[].
 *  b      float *  The blue brightness at each of the levels in lev[].
 *  n        int    The number of values in each of lev[],r[],g[] and b[].
 */
static void recolor_image(Image *im, float *lev, float *r, float *g, float *b,
			  int n)
{
  Boolean share;   /* True if the widget colors are readonly */
/*
 * Select the image PGPLOT widget and redefine its colors.
 */
  cpgslct(xap_device_id(im->w_image));
  cpgctab(lev, r, g, b, n, 1.0, 0.5);
/*
 * If the widget's colors were allocated readonly, redraw the image
 * to reveal the new colors.
 */
  XtVaGetValues(im->w_image, XapNshare, &share, NULL);
  if(share)
    display_image(im, xap_device_id(im->w_image));
}

void
centerWidgetAndMouse(Widget widget, Widget pwidget, Widget mwidget)
     /*
      * Center widget "widget" inside widget "pwidget" and warp mouse to middle
      * of "mwidget".
      */
{
	Arg args[2];
	Window rwin, child;
	int x, y, px, py;
	unsigned int w, h, pw, ph, bw, d;

	/* Get child size */
	XGetGeometry(XtDisplay(widget), XtWindow(widget),
		     &rwin, &x, &y, &w, &h, &bw, &d);
	/* Get parent size, position */
	XGetGeometry(XtDisplay(pwidget), XtWindow(pwidget),
		     &rwin, &px, &py, &pw, &ph, &bw, &d);
	/* Need position in root window coords, don't ask me why */
	XTranslateCoordinates(XtDisplay(widget), XtWindow(pwidget), rwin,
			      px, py, &x, &y, &child);
	px = x;
	py = y;
	/* Compute child position */
	x = px + pw / 2 - w / 2;
	if (x < 0)
		x = 0;
	else if (x > WidthOfScreen(XtScreen(widget)) - w)
		x = WidthOfScreen(XtScreen(widget)) - w;
	y = py + ph / 2 - h / 2;
	if (y < 0)
		y = 0;
	else if (y > HeightOfScreen(XtScreen(widget)) - h)
		y = WidthOfScreen(XtScreen(widget)) - h;
	/* Set child position */
	XtSetArg(args[0], XtNx, x);
	XtSetArg(args[1], XtNy, y);
	XtSetValues(widget, args, 2);
	/* Get dest size, position */
	XGetGeometry(XtDisplay(mwidget), XtWindow(mwidget),
		     &rwin, &x, &y, &w, &h, &bw, &d);
	/* Move mouse there */
	XWarpPointer(XtDisplay(mwidget), None, XtWindow(mwidget),
		     0, 0, 0, 0, w / 2, h / 2);
}
