//
// Generic drivers for cups-local.
//
// Copyright © 2023 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "cups-locald.h"
#include "icons.h"
#include <math.h>


//
// Local types...
//

typedef struct pcl_data_s		// PCL job data
{
  unsigned	width,			// Width
		height,			// Height
		xstart,			// First column on page/line
		xend,			// Last column on page/line
		ystart,			// First line on page
		yend;			// Last line on page
  int		compression;		// Compression mode
  size_t	line_size;		// Size of output line
  unsigned char	*line_buffer,		// Line buffer
		*comp_buffer;		// Compression buffer
  unsigned	feed;			// Number of lines to skip
} pcl_data_t;

typedef struct pcl_map_s		// PWG name to PCL code map
{
  const char	*keyword;		// Keyword string
  int		value;			// Value
} pcl_map_t;




//
// Local globals...
//

static const char * const pclps_media[] =
{       // Supported media sizes for Generic PCL/PostScript printers
  "na_ledger_11x17in",
  "na_legal_8.5x14in",
  "na_letter_8.5x11in",
  "na_executive_7x10in",
  "iso_a3_297x420mm",
  "iso_a4_210x297mm",
  "iso_a5_148x210mm",
  "jis_b5_182x257mm",
  "iso_b5_176x250mm",
  "na_number-10_4.125x9.5in",
  "iso_c5_162x229mm",
  "iso_dl_110x220mm",
  "na_monarch_3.875x7.5in"
};


//
// Local functions...
//

#if 0
static bool	eve_rendjob(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device);
static bool	eve_rendpage(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned page);
static bool	eve_rstartjob(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device);
static bool	eve_rstartpage(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned page);
static bool	eve_rwriteline(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned y, const unsigned char *pixels);
static bool	eve_status(pappl_printer_t *printer);
static bool	eve_update_status(pappl_printer_t *printer, pappl_device_t *device);
#endif // 0

static void	pcl_compress_data(pcl_data_t *pcl, pappl_device_t *device, unsigned y, const unsigned char *line, unsigned length);
static bool	pcl_rendjob(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device);
static bool	pcl_rendpage(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned page);
static bool	pcl_rstartjob(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device);
static bool	pcl_rstartpage(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned page);
static bool	pcl_rwriteline(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned y, const unsigned char *pixels);

static bool	pclps_print(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device);
static bool	pclps_status(pappl_printer_t *printer);
static bool	pclps_update_status(pappl_printer_t *printer, pappl_device_t *device);

static bool	ps_rendjob(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device);
static bool	ps_rendpage(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned page);
static bool	ps_rstartjob(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device);
static bool	ps_rstartpage(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned page);
static bool	ps_rwriteline(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned y, const unsigned char *pixels);


//
// 'LocalDriverAutoAdd()' - Determine a matching driver for a printer.
//

const char *				// O - Driver name
LocalDriverAutoAdd(
    const char *device_info,		// I - Device information
    const char *device_uri,		// I - Device URI
    const char *device_id,		// I - Device ID
    void       *cbdata)			// I - Callback data (unused)
{
  const char	*ret = NULL;		// Return value
  size_t	num_did;		// Number of device ID key/value pairs
  cups_option_t	*did;			// Device ID key/value pairs
  const char	*cmd,			// Command set
		*cmdptr;		// Pointer into command set


  (void)device_info;
  (void)cbdata;

  // IPP printers always use the "everywhere" driver...
  if (!strncmp(device_uri, "ipp://", 6) || !strncmp(device_uri, "ipps://", 7))
  {
    ret = "everywhere";
  }
  else
  {
    // PCL and PostScript printers are detected using the 1284 command set value
    num_did = papplDeviceParseID(device_id, &did);

    if ((cmd = cupsGetOption("COMMAND SET", num_did, did)) == NULL)
      cmd = cupsGetOption("CMD", num_did, did);

    if (cmd && (cmdptr = strstr(cmd, "PCL")) != NULL && (cmdptr[3] == ',' || cmdptr[3] == '3' || cmdptr[3] == '5' || !cmdptr[3]))
      ret = "pcl";
    else if (cmd && (cmdptr = strstr(cmd, "POSTSCRIPT")) != NULL && (cmdptr[10] == ',' || !cmdptr[10]))
      ret = "ps";
    else if (cmd && strstr(cmd, "PostScript Level 3 Emulation") != NULL)
      ret = "ps";

    cupsFreeOptions(num_did, did);
  }

  return (ret);
}


//
// 'LocalDriverCallback()' - Setup driver data.
//

bool
LocalDriverCallback(
    pappl_system_t         *system,	// I - System (unused)
    const char             *driver_name,// I - Driver name
    const char             *device_uri,	// I - Device URI
    const char             *device_id,	// I - IEEE-1284 device ID (unused)
    pappl_pr_driver_data_t *data,	// O - Printer driver data
    ipp_t                  **attrs,	// O - Printer driver attributes (unused)
    void                   *cbdata)	// I - Callback data (unused)
{
  size_t   		i, j;		// Looping variables
  static pappl_dither_t	dither =	// Blue-noise dither array
  {
    { 111,  49, 142, 162, 113, 195,  71, 177, 201,  50, 151,  94,  66,  37,  85, 252 },
    {  25,  99, 239, 222,  32, 250, 148,  19,  38, 106, 220, 170, 194, 138,  13, 167 },
    { 125, 178,  79,  15,  65, 173, 123,  87, 213, 131, 247,  23, 116,  54, 229, 212 },
    {  41, 202, 152, 132, 189, 104,  53, 236, 161,  62,   1, 181,  77, 241, 147,  68 },
    {   2, 244,  56,  91, 230,   5, 204,  28, 187, 101, 144, 206,  33,  92, 190, 107 },
    { 223, 164, 114,  36, 214, 156, 139,  70, 245,  84, 226,  48, 126, 158,  17, 135 },
    {  83, 196,  21, 254,  76,  45, 179, 115,  12,  40, 169, 105, 253, 176, 211,  59 },
    { 100, 180, 145, 122, 172,  97, 235, 129, 215, 149, 199,   8,  72,  26, 238,  44 },
    { 232,  31,  69,  11, 205,  58,  18, 193,  88,  60, 112, 221, 140,  86, 120, 153 },
    { 208, 130, 243, 160, 224, 110,  34, 248, 165,  24, 234, 184,  52, 198, 171,   6 },
    { 108, 188,  51,  89, 137, 186, 154,  78,  47, 134,  98, 157,  35, 249,  95,  63 },
    {  16,  75, 219,  39,   0,  67, 228, 121, 197, 240,   3,  74, 127,  20, 227, 143 },
    { 246, 175, 119, 200, 251, 103, 146,  14, 209, 174, 109, 218, 192,  82, 203, 163 },
    {  29,  93, 150,  22, 166, 182,  55,  30,  90,  64,  42, 141, 168,  57, 117,  46 },
    { 216, 233,  61, 128,  81, 237, 217, 118, 159, 255, 185,  27, 242, 102,   4, 133 },
    {  73, 191,   9, 210,  43,  96,   7, 136, 231,  80,  10, 124, 225, 207, 155, 183 }
  };


  (void)system;
  (void)device_id;
  (void)attrs;
  (void)cbdata;

  if (!strcmp(driver_name, "everywhere"))
  {
    // Query the printer for capabilities...

    // TODO: Query IPP printer for capabilities
    /* Default icons */
    data->icons[0].data    = everywhere_sm_png;
    data->icons[0].datalen = sizeof(everywhere_sm_png);

    data->icons[1].data    = everywhere_md_png;
    data->icons[1].datalen = sizeof(everywhere_md_png);

    data->icons[2].data    = everywhere_lg_png;
    data->icons[2].datalen = sizeof(everywhere_lg_png);
  }
  else
  {
    // Use generic capabilities for a B&W laser printer...

    // Dither arrays...
    for (i = 0; i < 16; i ++)
    {
      // Apply gamma correction to dither array...
      for (j = 0; j < 16; j ++)
	data->gdither[i][j] = 255 - (int)(255.0 * pow(1.0 - dither[i][j] / 255.0, 0.4545));
    }

    memcpy(data->pdither, data->gdither, sizeof(data->pdither));

    /* Default orientation and quality */
    data->orient_default  = IPP_ORIENT_NONE;
    data->quality_default = IPP_QUALITY_NORMAL;

    /* Pages-per-minute for monochrome and color */
    data->ppm = 8;
    if (strstr(driver_name, "_color") != NULL)
      data->ppm_color = 2;
    else
      data->ppm_color = 0;

    /* Three resolutions - 150dpi, 300dpi (default), and 600dpi */
    data->num_resolution  = 3;
    data->x_resolution[0] = 150;
    data->y_resolution[0] = 150;
    data->x_resolution[1] = 300;
    data->y_resolution[1] = 300;
    data->x_resolution[2] = 600;
    data->y_resolution[2] = 600;
    data->x_default       = data->y_default = 300;

    /* Media sizes */
    data->num_media = sizeof(pclps_media) / sizeof(pclps_media[0]);
    memcpy(data->media, pclps_media, sizeof(pclps_media));

    /* Media sources */
    data->num_source = 7;
    data->source[0]  = "default";
    data->source[1]  = "tray-1";
    data->source[2]  = "tray-2";
    data->source[3]  = "tray-3";
    data->source[4]  = "tray-4";
    data->source[5]  = "manual";
    data->source[6]  = "envelope";

    /* Media types */
    data->num_type = 6;
    data->type[0]  = "stationery";
    data->type[1]  = "stationery-letterhead";
    data->type[2]  = "cardstock";
    data->type[3]  = "labels";
    data->type[4]  = "envelope";
    data->type[5]  = "transparency";

    for (i = 0; i < data->num_source; i ++)
    {
      if (strcmp(data->source[i], "envelope"))
	cupsCopyString(data->media_ready[i].size_name, "na_letter_8.5x11in", sizeof(data->media_ready[i].size_name));
      else
	cupsCopyString(data->media_ready[i].size_name, "env_10_4.125x9.5in", sizeof(data->media_ready[i].size_name));
    }

    /* Duplex */
    if (strstr(driver_name, "_duplex") != NULL)
    {
      /* 1-sided printing only */
      data->sides_supported = PAPPL_SIDES_ONE_SIDED;
      data->sides_default   = PAPPL_SIDES_ONE_SIDED;
    }
    else
    {
      /* 1- or 2-sided printing */
      data->sides_supported = PAPPL_SIDES_ONE_SIDED | PAPPL_SIDES_TWO_SIDED_LONG_EDGE | PAPPL_SIDES_TWO_SIDED_SHORT_EDGE;
      data->sides_default   = PAPPL_SIDES_TWO_SIDED_LONG_EDGE;
    }

    if (!strncmp(driver_name, "pcl", 3))
    {
      // PCL

      /* Make and model name */
      snprintf(data->make_and_model, sizeof(data->make_and_model), "Generic PCL%s", strstr(driver_name, "_duplex") != NULL ? " w/Duplexer" : "");

      /* Native format */
      data->format = "application/vnd.hp-pcl";

      /* Icons */
      data->icons[0].data    = pcl_sm_png;
      data->icons[0].datalen = sizeof(pcl_sm_png);

      data->icons[1].data    = pcl_md_png;
      data->icons[1].datalen = sizeof(pcl_md_png);

      data->icons[2].data    = pcl_lg_png;
      data->icons[2].datalen = sizeof(pcl_lg_png);

      /* Margins */
      data->left_right = 635;	 // 1/4" left and right
      data->bottom_top = 1270;	 // 1/2" top and bottom

      /* Three color spaces - black (1-bit and 8-bit) and grayscale */
      data->raster_types = PAPPL_PWG_RASTER_TYPE_BLACK_1 | PAPPL_PWG_RASTER_TYPE_BLACK_8 | PAPPL_PWG_RASTER_TYPE_SGRAY_8;

      /* Color modes: auto (default), monochrome, and color */
      data->color_supported = PAPPL_COLOR_MODE_AUTO | PAPPL_COLOR_MODE_AUTO_MONOCHROME | PAPPL_COLOR_MODE_MONOCHROME;
      data->color_default   = PAPPL_COLOR_MODE_AUTO;

      /* Set callbacks */
      data->printfile_cb  = pclps_print;
      data->rendjob_cb    = pcl_rendjob;
      data->rendpage_cb   = pcl_rendpage;
      data->rstartjob_cb  = pcl_rstartjob;
      data->rstartpage_cb = pcl_rstartpage;
      data->rwriteline_cb = pcl_rwriteline;
      data->status_cb     = pclps_status;
      data->has_supplies  = true;
    }
    else
    {
      // PostScript

      /* Make and model name */
      snprintf(data->make_and_model, sizeof(data->make_and_model), "Generic %sPostScript%s", strstr(driver_name, "_color") != NULL ? "Color " : "", strstr(driver_name, "_duplex") != NULL ? " w/Duplexer" : "");

      /* Native format */
      data->format = "application/postscript";

      /* Icons */
      data->icons[0].data    = ps_sm_png;
      data->icons[0].datalen = sizeof(ps_sm_png);

      data->icons[1].data    = ps_md_png;
      data->icons[1].datalen = sizeof(ps_md_png);

      data->icons[2].data    = ps_lg_png;
      data->icons[2].datalen = sizeof(ps_lg_png);

      /* Margins */
      data->left_right = 423;	 // 1/6" left and right
      data->bottom_top = 423;	 // 1/6" top and bottom

      /* Four color spaces - black (1-bit and 8-bit), grayscale, and sRGB */
      data->raster_types = PAPPL_PWG_RASTER_TYPE_BLACK_1 | PAPPL_PWG_RASTER_TYPE_BLACK_8 | PAPPL_PWG_RASTER_TYPE_SGRAY_8 | PAPPL_PWG_RASTER_TYPE_SRGB_8;

      /* Color modes: auto (default), monochrome, and color */
      data->color_supported = PAPPL_COLOR_MODE_AUTO | PAPPL_COLOR_MODE_AUTO_MONOCHROME | PAPPL_COLOR_MODE_COLOR | PAPPL_COLOR_MODE_MONOCHROME;
      data->color_default   = PAPPL_COLOR_MODE_AUTO;

      /* Set callbacks */
      data->printfile_cb  = pclps_print;
      data->rendjob_cb    = ps_rendjob;
      data->rendpage_cb   = ps_rendpage;
      data->rstartjob_cb  = ps_rstartjob;
      data->rstartpage_cb = ps_rstartpage;
      data->rwriteline_cb = ps_rwriteline;
      data->status_cb     = pclps_status;
      data->has_supplies  = true;
    }
  }

  return (true);
}


//
// 'pcl_compress_data()' - Compress a line of graphics.
//

static void
pcl_compress_data(
    pcl_data_t          *pcl,		// I - Job data
    pappl_device_t      *device,	// I - Device
    unsigned            y,		// I - Line number
    const unsigned char *line,		// I - Data to compress
    unsigned            length)		// I - Number of bytes
{
  const unsigned char	*line_ptr,	// Current byte pointer
			*line_end,	// End-of-line byte pointer
			*start;		// Start of compression sequence
  unsigned char		*comp_ptr;	// Pointer into compression buffer
  unsigned		count;		// Count of bytes for output
  int			comp;		// Current compression type


  // Try doing TIFF PackBits compression...
  line_ptr = line;
  line_end = line + length;
  comp_ptr = pcl->comp_buffer;

  while (line_ptr < line_end)
  {
    if ((line_ptr + 1) >= line_end)
    {
      // Single byte on the end...
      *comp_ptr++ = 0x00;
      *comp_ptr++ = *line_ptr++;
    }
    else if (line_ptr[0] == line_ptr[1])
    {
      // Repeated sequence...
      line_ptr ++;
      count = 2;

      while (line_ptr < (line_end - 1) && line_ptr[0] == line_ptr[1] && count < 128)
      {
	line_ptr ++;
	count ++;
      }

      *comp_ptr++ = (unsigned char)(257 - count);
      *comp_ptr++ = *line_ptr++;
    }
    else
    {
      // Non-repeated sequence...
      start    = line_ptr;
      line_ptr ++;
      count    = 1;

      while (line_ptr < (line_end - 1) && line_ptr[0] != line_ptr[1] && count < 128)
      {
	line_ptr ++;
	count ++;
      }

      *comp_ptr++ = (unsigned char)(count - 1);

      memcpy(comp_ptr, start, count);
      comp_ptr += count;
    }
  }

  if ((unsigned)(comp_ptr - pcl->comp_buffer) > length)
  {
    // Don't try compressing...
    comp     = 0;
    line_ptr = pcl->comp_buffer;
    line_end = pcl->comp_buffer + length;
  }
  else
  {
    // Use PackBits compression...
    comp     = 2;
    line_ptr = pcl->comp_buffer;
    line_end = comp_ptr;
  }

  // Set compression mode as needed...
  if (pcl->compression != comp)
  {
    // Set compression
    pcl->compression = comp;
    papplDevicePrintf(device, "\033*b%uM", pcl->compression);
  }

  // Set the length of the data and write a raster plane...
  papplDevicePrintf(device, "\033*b%dW", (int)(line_end - line_ptr));
  papplDeviceWrite(device, line_ptr, (size_t)(line_end - line_ptr));
}


//
// 'pcl_rendjob()' - End a job.
//

static bool				// O - `true` on success, `false` on failure
pcl_rendjob(
    pappl_job_t        *job,		// I - Job
    pappl_pr_options_t *options,	// I - Options
    pappl_device_t     *device)		// I - Device
{
  pcl_data_t	*pcl = (pcl_data_t *)papplJobGetData(job);
					// Job data


  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Ending job...");

  (void)options;

  papplDevicePuts(device, "\033E");

  free(pcl);
  papplJobSetData(job, NULL);

  pclps_update_status(papplJobGetPrinter(job), device);

  return (true);
}


//
// 'pcl_rendpage()' - End a page.
//

static bool				// O - `true` on success, `false` on failure
pcl_rendpage(
    pappl_job_t        *job,		// I - Job
    pappl_pr_options_t *options,	// I - Job options
    pappl_device_t     *device,		// I - Device
    unsigned           page)		// I - Page number
{
  pcl_data_t	*pcl = (pcl_data_t *)papplJobGetData(job);
					// Job data


  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Ending page %u...", page);

  // Eject the current page...
  papplDevicePuts(device, "\033*r0B");// End GFX

  if (!(options->header.Duplex && (page & 1)))
    papplDevicePuts(device, "\014");  // Eject current page

  papplDeviceFlush(device);

  // Free memory...
  free(pcl->line_buffer);
  free(pcl->comp_buffer);

  return (true);
}


//
// 'pcl_rstartjob()' - Start a job.
//

static bool				// O - `true` on success, `false` on failure
pcl_rstartjob(
    pappl_job_t        *job,		// I - Job
    pappl_pr_options_t *options,	// I - Job options
    pappl_device_t     *device)		// I - Device
{
  pcl_data_t	*pcl = (pcl_data_t *)calloc(1, sizeof(pcl_data_t));
					// Job data


  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Starting job...");

  pclps_update_status(papplJobGetPrinter(job), device);

  (void)options;

  papplJobSetData(job, pcl);

  // Send a PCL reset sequence
  papplDevicePuts(device, "\033E");

  return (true);
}


//
// 'pcl_rstartpage()' - Start a page.
//

static bool				// O - `true` on success, `false` on failure
pcl_rstartpage(
    pappl_job_t        *job,		// I - Job
    pappl_pr_options_t *options,	// I - Job options
    pappl_device_t     *device,		// I - Device
    unsigned           page)		// I - Page number
{
  size_t	i;			// Looping var
  cups_page_header_t *header = &(options->header);
					// Page header
  pcl_data_t	*pcl = (pcl_data_t *)papplJobGetData(job);
					// Job data
  static const pcl_map_t pcl_sizes[] =	// PCL media size values
  {
    { "iso_a3_297x420mm",		27 },
    { "iso_a4_210x297mm",		26 },
    { "iso_a5_148x210mm",		25 },
    { "iso_b5_176x250mm",		100 },
    { "iso_c5_162x229mm",		91 },
    { "iso_dl_110x220mm",		90 },
    { "jis_b5_182x257mm",		45 },
    { "na_executive_7x10in",		1 },
    { "na_ledger_11x17in",		6 },
    { "na_legal_8.5x14in",		3 },
    { "na_letter_8.5x11in",		2 },
    { "na_monarch_3.875x7.5in",		80 },
    { "na_number-10_4.125x9.5in",	81 }
  };
  static const pcl_map_t pcl_sources[] =// PCL media source values
  {
    { "auto",		7 },
    { "by-pass-tray",	4 },
    { "disc",		14 },
    { "envelope",	6 },
    { "large-capacity",	5 },
    { "main",		1 },
    { "manual",		2 },
    { "right",		8 },
    { "tray-1",		20 },
    { "tray-2",		21 },
    { "tray-3",		22 },
    { "tray-4",		23 },
    { "tray-5",		24 },
    { "tray-6",		25 },
    { "tray-7",		26 },
    { "tray-8",		27 },
    { "tray-9",		28 },
    { "tray-10",	29 },
    { "tray-11",	30 },
    { "tray-12",	31 },
    { "tray-13",	32 },
    { "tray-14",	33 },
    { "tray-15",	34 },
    { "tray-16",	35 },
    { "tray-17",	36 },
    { "tray-18",	37 },
    { "tray-19",	38 },
    { "tray-20",	39 }
  };
  static const pcl_map_t pcl_data_types[] =	// PCL media type values
  {
    { "disc",			7 },
    { "photographic",		3 },
    { "stationery-inkjet",	2 },
    { "stationery",		0 },
    { "transparency",		4 }
  };


  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Starting page %u...", page);

  // Setup size based on margins...
  pcl->width  = options->printer_resolution[0] * (options->media.size_width - options->media.left_margin - options->media.right_margin) / 2540;
  pcl->height = options->printer_resolution[1] * (options->media.size_length - options->media.top_margin - options->media.bottom_margin) / 2540;
  pcl->xstart = options->printer_resolution[0] * options->media.left_margin / 2540;
  pcl->xend   = pcl->xstart + pcl->width;
  pcl->ystart = options->printer_resolution[1] * options->media.top_margin / 2540;
  pcl->yend   = pcl->ystart + pcl->height;

  // Setup printer/job attributes...
  if (options->sides == PAPPL_SIDES_ONE_SIDED || (page & 1))
  {
    // Set media position
    for (i = 0; i < (sizeof(pcl_sources) / sizeof(pcl_sources[0])); i ++)
    {
      if (!strcmp(options->media.source, pcl_sources[i].keyword))
      {
	papplDevicePrintf(device, "\033&l%dH", pcl_sources[i].value);
	break;
      }
    }

    // Set 6 LPI, 10 CPI
    papplDevicePuts(device, "\033&l6D\033&k12H");

    // Set portrait orientation
    papplDevicePuts(device, "\033&l0O");

    // Set page size
    for (i = 0; i < (sizeof(pcl_sizes) / sizeof(pcl_sizes[0])); i ++)
    {
      if (!strcmp(options->media.size_name, pcl_sizes[i].keyword))
      {
	papplDevicePrintf(device, "\033&l%dA", pcl_sizes[i].value);
	break;
      }
    }

    if (i >= (sizeof(pcl_sizes) / sizeof(pcl_sizes[0])))
    {
      // Custom size, set page length...
      papplDevicePrintf(device, "\033&l%dP", 6 * options->media.size_length / 2540);
    }

    // Set media type
    for (i = 0; i < (sizeof(pcl_data_types) / sizeof(pcl_data_types[0])); i ++)
    {
      if (!strcmp(options->media.type, pcl_data_types[i].keyword))
      {
	papplDevicePrintf(device, "\033&l%dM", pcl_data_types[i].value);
	break;
      }
    }

    // Set top margin to 0
    papplDevicePuts(device, "\033&l0E");

    // Turn off perforation skip
    papplDevicePuts(device, "\033&l0L");

    // Set duplex mode...
    switch (options->sides)
    {
      case PAPPL_SIDES_ONE_SIDED :
	  papplDevicePuts(device, "\033&l0S");
	  break;
      case PAPPL_SIDES_TWO_SIDED_LONG_EDGE :
	  papplDevicePuts(device, "\033&l2S");
	  break;
      case PAPPL_SIDES_TWO_SIDED_SHORT_EDGE :
	  papplDevicePuts(device, "\033&l1S");
	  break;
    }
  }
  else
  {
    // Set back side
    papplDevicePuts(device, "\033&a2G");
  }

  // Set resolution
  papplDevicePrintf(device, "\033*t%uR", header->HWResolution[0]);

  // Set size
  papplDevicePrintf(device, "\033*r%uS\033*r%uT", pcl->width, pcl->height);

  // Set position
  papplDevicePrintf(device, "\033&a0H\033&a%.0fV", 720.0 * options->media.top_margin / 2540.0);

  // Start graphics
  papplDevicePuts(device, "\033*r1A");

  // Allocate dithering plane buffers
  pcl->line_size = (pcl->width + 7) / 8;

  if ((pcl->line_buffer = malloc(pcl->line_size)) == NULL)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Memory allocation failure.");
    return (false);
  }

  // No blank lines yet...
  pcl->feed = 0;

  // Allocate memory for compression...
  if ((pcl->comp_buffer = malloc(pcl->line_size * 2 + 2)) == NULL)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Memory allocation failure.");
    return (false);
  }

  return (true);
}


//
// 'pcl_rwriteline()' - Write a line.
//

static bool				// O - `true` on success, `false` on failure
pcl_rwriteline(
    pappl_job_t         *job,		// I - Job
    pappl_pr_options_t  *options,	// I - Job options
    pappl_device_t      *device,	// I - Device
    unsigned            y,		// I - Line number
    const unsigned char *pixels)	// I - Line
{
  cups_page_header_t	*header = &(options->header);
					// Page header
  pcl_data_t		*pcl = (pcl_data_t *)papplJobGetData(job);
					// Job data
  unsigned		x;		// Current column
  const unsigned char	*pixptr;	// Pixel pointer in line
  unsigned char		bit,		// Current plane data
			*kptr,		// Pointer into k-plane
			byte;		// Byte in line
  const unsigned char	*dither;	// Dither line


  // Skip top and bottom margin areas...
  if (y < pcl->ystart || y >= pcl->yend)
    return (true);

  if (!(y & 127))
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Printing line %u (%u%%)", y, 100 * (y - pcl->ystart) / pcl->height);

  // Check whether the line is all whitespace...
  byte = options->header.cupsColorSpace == CUPS_CSPACE_K ? 0 : 255;

  if (*pixels != byte || memcmp(pixels, pixels + 1, header->cupsBytesPerLine - 1))
  {
    // No, skip previous whitespace as needed...
    if (pcl->feed > 0)
    {
      papplDevicePrintf(device, "\033*b%dY", pcl->feed);
      pcl->feed = 0;
    }

    // Dither bitmap data...
    dither = options->dither[y & 15];

    if (header->cupsBitsPerPixel == 8)
    {
      memset(pcl->line_buffer, 0, pcl->line_size);

      if (header->cupsColorSpace == CUPS_CSPACE_K)
      {
	// 8 bit black
	for (x = pcl->xstart, kptr = pcl->line_buffer, pixptr = pixels + pcl->xstart, bit = 128, byte = 0; x < pcl->xend; x ++, pixptr ++)
	{
	  if (*pixptr >= dither[x & 15])
	    byte |= bit;

	  if (bit == 1)
	  {
	    *kptr++ = byte;
	    byte    = 0;
	    bit     = 128;
	  }
	  else
	    bit /= 2;
	}

	if (bit < 128)
	  *kptr = byte;
      }
      else
      {
	// 8 bit gray
	for (x = pcl->xstart, kptr = pcl->line_buffer, pixptr = pixels + pcl->xstart, bit = 128, byte = 0; x < pcl->xend; x ++, pixptr ++)
	{
	  if (*pixptr < dither[x & 15])
	    byte |= bit;

	  if (bit == 1)
	  {
	    *kptr++ = byte;
	    byte    = 0;
	    bit     = 128;
	  }
	  else
	    bit /= 2;
	}

	if (bit < 128)
	  *kptr = byte;
      }
    }
    else
    {
      // 1-bit B&W
      memcpy(pcl->line_buffer, pixels + pcl->xstart / 8, pcl->line_size);
    }

    pcl_compress_data(pcl, device, y, pcl->line_buffer, pcl->line_size);
    papplDeviceFlush(device);
  }
  else
  {
    pcl->feed ++;
  }

  return (true);
}


//
// 'pclps_print()' - Print file.
//

static bool				// O - `true` on success, `false` on failure
pclps_print(
    pappl_job_t        *job,		// I - Job
    pappl_pr_options_t *options,	// I - Options
    pappl_device_t     *device)		// I - Device
{
  int		fd;			// Job file
  ssize_t	bytes;			// Bytes read/written
  char		buffer[65536];		// Read/write buffer


  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Printing raw file...");

  papplJobSetImpressions(job, 1);

  fd = open(papplJobGetFilename(job), O_RDONLY);

  while ((bytes = read(fd, buffer, sizeof(buffer))) > 0)
  {
    if (papplDeviceWrite(device, buffer, (size_t)bytes) < 0)
    {
      papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Unable to send %d bytes to printer.", (int)bytes);
      close(fd);
      return (false);
    }
  }

  close(fd);

  papplJobSetImpressionsCompleted(job, 1);

  return (true);
}


//
// 'pclps_status()' - Get printer status.
//

static bool				// O - `true` on success, `false` on failure
pclps_status(
    pappl_printer_t *printer)		// I - Printer
{
  pappl_device_t	*device;	// Printer device
  pappl_supply_t	supply[32];	// Printer supply information
  static pappl_supply_t defsupply[4] =	// Default supply level data
  {
    { PAPPL_SUPPLY_COLOR_BLACK,   "Black Toner",   true, 80, PAPPL_SUPPLY_TYPE_TONER },
    { PAPPL_SUPPLY_COLOR_CYAN,    "Cyan Toner",    true, 80, PAPPL_SUPPLY_TYPE_TONER },
    { PAPPL_SUPPLY_COLOR_MAGENTA, "Magenta Toner", true, 80, PAPPL_SUPPLY_TYPE_TONER },
    { PAPPL_SUPPLY_COLOR_YELLOW,  "Yellow Toner",  true, 80, PAPPL_SUPPLY_TYPE_TONER }
  };


  if (papplPrinterGetSupplies(printer, 0, supply) > 0)
  {
    // Already have supplies, just return...
    return (true);
  }

  papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Checking status...");

  // First try to query the supply levels via SNMP...
  if ((device = papplPrinterOpenDevice(printer)) != NULL)
  {
    bool success = pclps_update_status(printer, device);

    papplPrinterCloseDevice(printer);

    if (success)
      return (true);
  }

  // Otherwise make sure we have some dummy data to make clients happy...
  if (strstr(papplPrinterGetDriverName(printer), "_color") != NULL)
    papplPrinterSetSupplies(printer, 4, defsupply);
  else
    papplPrinterSetSupplies(printer, 1, defsupply);

  return (true);
}


//
// 'pclps_update_status()' - Update the supply levels and status.
//

static bool				// O - `true` on success, `false` otherwise
pclps_update_status(
    pappl_printer_t *printer,		// I - Printer
    pappl_device_t  *device)		// I - Device
{
  int			num_supply;	// Number of supplies
  pappl_supply_t	supply[32];	// Printer supply information


  if ((num_supply = papplDeviceGetSupplies(device, sizeof(supply) / sizeof(supply[0]), supply)) > 0)
    papplPrinterSetSupplies(printer, num_supply, supply);

  papplPrinterSetReasons(printer, papplDeviceGetStatus(device), PAPPL_PREASON_DEVICE_STATUS);

  return (num_supply > 0);
}


//
// 'ps_rendjob()' - End a graphics job.
//

static bool
ps_rendjob(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device)
{
  (void)job;
  (void)options;
  (void)device;

  return (false);
}


//
// 'ps_rendpage()' - End a page of graphics.
//

static bool
ps_rendpage(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned page)
{
  (void)job;
  (void)options;
  (void)device;
  (void)page;

  return (false);
}


//
// 'ps_rstartjob()' - Start a graphics job.
//

static bool
ps_rstartjob(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device)
{
  (void)job;
  (void)options;
  (void)device;

  return (false);
}


//
// 'ps_rstartpage()' - Start a page of graphics.
//

static bool
ps_rstartpage(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned page)
{
  (void)job;
  (void)options;
  (void)device;
  (void)page;

  return (false);
}


//
// 'ps_rwriteline()' - Write a line of graphics.
//

static bool
ps_rwriteline(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned y, const unsigned char *pixels)
{
  (void)job;
  (void)options;
  (void)device;
  (void)y;
  (void)pixels;

  return (false);
}
