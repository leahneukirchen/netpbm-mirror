#include <netpbm/pam.h>
#include <netpbm/shhopt.h>
#include <netpbm/mallocvar.h>

#include <stdbool.h>
#include <stdint.h>

#define MAX_MAXVAL      65535
#define MAX_NUM_ATTRIBS     20
#define MAX_Z           ((1 << 30) - 1)

/*----------------------------------------------------------------------------
 Struct definitions
----------------------------------------------------------------------------*/


typedef struct {
/*----------------------------------------------------------------------------
    This struct and the functions that manipulate variables of this type act
    as a substitute for floating point computations. Here, whenever we need a
    value with a fractional component, we represent it using two parts: 1. An
    integer part, called the "quotient", and 2. A fractional part, which is
    itself composed of a "remainder" (or "numerator") and a "divisor" (or
    "denominator"). The fract struct provides storage for the quotient and the
    remainder, but the divisor must be given separately (because it often
    happens in this program that whenever we are dealing with one variable of
    type fract, we are dealing with more of them at the same time, and they
    all have the same divisor).

    To be more precise, the way we actually use variables of this type works
    like this: We read integer values through standard input; When drawing
    triangles, we need need to calculate differences between some pairs of
    these input values and divide such differences by some other integer,
    which is the above mentioned divisor. That result is then used to compute
    successive interpolations between the two values for which we had
    originally calculated the difference, and is therefore called the
    "interpolation step". The values between which we wish to take successive
    interpolations are called the "initial value" and the "final value". The
    interpolation procedure works like this: First, we transform the initial
    value into a fract variable by equating the quotient of that variable to
    the initial value and assigning 0 to its remainder. Then, we successivelly
    apply the interpolation step to that variable through successive calls to
    step_up and/or multi_step_up until the quotient of the variable equals the
    final value. Each application of step_up or multi_step_up yields a
    particular linear interpolation between the initial and final values.

    If and only if a particular fract variable represents an interpolation
    step, the "negative_flag" field indicates whether the step is negative
    (i. e. negative_flag == true) or not (negative_flag == false). This is
    necessary in order to make sure that variables are "stepped up" in the
    appropriate direction, so to speak, as the field which stores the
    remainder in any fract variable, "r", is always equal to or above 0, and
    the quotient of a step may be 0, so the actual sign of the step value is
    not always discoverable through a simple examination of the sign of the
    quotient. On the other hand, if the variable does not represent an
    interpolation step, the negative_flag is meaningless.
-----------------------------------------------------------------------------*/
    int32_t q;     /* Quotient */
    int32_t r: 31; /* Remainder */
    bool    negative_flag: 1;
} fract;



/* Each of the following structs has only one instance, which are created in
   the main function.
*/

typedef struct {
/*----------------------------------------------------------------------------
   Information about the frame buffer and PAM output
-----------------------------------------------------------------------------*/
    /* These fields are initialized once by reading the command line
       arguments. "maxval" and "num_attribs" may be modified later
       through "realloc_image_buffer".
    */
    int32_t width;
    int32_t height;
    int32_t maxval;
    int32_t num_attribs;

    /* The fields below must be initialized by "init_framebuffer" and
       freed by "free_framebuffer", except for the tuple_type field in
       "outpam" which is initialized once by reading the command line
       arguments and may be modified later through "set_tupletype".
    */
    struct {
        uint16_t * buffer;
        uint32_t   bytes;
    } img; /* Image buffer */

    struct {
        uint32_t * buffer;
        uint32_t   bytes;
    } z;  /* Z-buffer */

    struct pam outpam;

    tuple * pamrow;
} framebuffer_info;



typedef struct {
/*----------------------------------------------------------------------------
  Information about visible triangle rows' boundaries. Also see the
  "boundary buffer functions" below.

  A "visible" triangle row is one which:

    1. Corresponds to a frame buffer row whose index (from top to bottom) is
       equal to or greater than 0 and smaller than the image height; and

    2. Has at least some of its pixels between the frame buffer columns whose
       index (from left to right) is equal to or greater than 0 and smaller
       than the image width.
-----------------------------------------------------------------------------*/
    int16_t start_scanline;
        /* Index of the frame buffer scanline which contains the first visible
           row of the current triangle, if there is any such row. If not, it
           contains the value -1.
        */

    int16_t num_upper_rows;
        /* The number of visible rows in the upper part of the triangle. The
           upper part of a triangle is composed of all the rows starting from
           the top vertex down to the middle vertex, but not including this
           last one.
        */

    int16_t num_lower_rows;
        /* The number of visible rows in the lower part of the triangle. The
           lower part of a triangle is composed of all the rows from the
           middle vertex to the bottom vertex -- all inclusive.
        */

    int16_t * buffer;
        /* This is the "boundary buffer": a pointer to an array of int16_t's
           where each consecutive pair of values indicates, in this order, the
           columns of the left and right boundary pixels for a particular
           visible triangle row. Those boundaries are inclusive on both sides
           and may be outside the limits of the frame buffer. This field is
           initialized and freed by the functions "init_boundary_buffer" and
           "free_boundary_buffer", respectively.
        */
} boundary_info;

typedef struct {
/*----------------------------------------------------------------------------
  Information necessary for the "process_next_command" function.  It must be
  initialized through "init_input_processor" and freed by
  "free_input_processor".
-----------------------------------------------------------------------------*/
    char *   buffer;
    size_t   length;
    uint64_t number;
} input_info;

/*----------------------------------------------------------------------------
   Utility functions
-----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
  Generate the interpolation steps for a collection of initial and final
  values. "begin" points to an array of initial values, "end" points to the
  array of corresponding final values; each interpolation step is stored in
  the appropriate position in the array pointed by "out"; "elements" indicates
  the number of elements in each of the previously mentioned arrays and
  "divisor" is the common value by which we want to divide the difference
  between each element in the array pointed to by "end" and the corresponding
  element in the array pointed to by "begin".  After an execution of this
  function, for each out[i], with 0 <= i < elements, the following will hold:

    1. If divisor > 1:
      out[i].q = (end[i] - begin[i]) / divisor
      out[i].r = abs((end[i] - begin[i]) % divisor)

    2. If divisor == 1 || divisor == 0:
      out[i].q = end[i] - begin[i]
      out[i].r = 0
-----------------------------------------------------------------------------*/
void
gen_steps(const int32_t * begin,
          const int32_t * end,
          fract *         out,
          uint8_t         elements,
          int32_t         divisor);

/*----------------------------------------------------------------------------
  Apply interpolation steps (see above) to a collection of fract
  variables (also see above) once. This is done by adding the
  quotient of each step to the quotient of the corresponding variable
  and the remainder of that step to the remainder of the variable. If the
  remainder of the variable becomes equal to or larger than the
  divisor, we increment the quotient of the variable if the negetive_flag
  of the step is false, or decrement it if the negetive_flag is true, and
  subtract the divisor from the remainder of the variable (in both cases).

  It *is* safe to pass a 0 divisor to this function.
-----------------------------------------------------------------------------*/
void
step_up(fract *       vars,
        const fract * steps,
        uint8_t       elements,
        int32_t       divisor);

/*----------------------------------------------------------------------------
  Similar to step_up, but apply the interpolation step an arbitrary number
  of times, instead of just once.

  It *is* also safe to pass a 0 divisor to this function.
-----------------------------------------------------------------------------*/
void
multi_step_up(fract *       vars,
              const fract * steps,
              uint8_t       elements,
              int32_t       times,
              int32_t       divisor);

void
fract_to_int32_array(const fract * in,
                     int32_t     * out,
                     uint8_t       elements);

void
int32_to_fract_array(const int32_t * in,
                     fract *         out,
                     uint8_t         elements);

/*----------------------------------------------------------------------------
  Sort an index array of 3 elements. This function is used to sort vertices
  with regard to relative row from top to bottom, but instead of sorting
  an array of vertices with all their coordinates, we simply sort their
  indices. Each element in the array pointed to by "index_array" should
  contain one of the numbers 0, 1 or 2, and each one of them should be
  different. "y_array" should point to an array containing the corresponding
  Y coordinates (row) of each vertex and "x_array" should point to an array
  containing the corresponding X coordinates (column) of each vertex.

  If the Y coordinates are all equal, the indices are sorted with regard to
  relative X coordinate from left to right. If only the top two vertex have
  the same Y coordinate, the array is sorted normally with regard to relative
  Y coordinate, but the first two indices are then sorted with regard to
  relative X coordinate. Finally, If only the bottom two vertex have the same
  Y coordinate, the array is sorted normally with regard to relative Y
  coordinate, but the last two indices are then sorted with regard to relative
  X coordinate.
-----------------------------------------------------------------------------*/
void
sort3(uint8_t *       index_array,
      const int32_t * y_array,
      const int32_t * x_array);

/*----------------------------------------------------------------------------
   Frame buffer functions
------------------------------------------------------------------------------

  Every drawing operation is applied on an internal "frame buffer", which is
  simply an "image buffer" which represents the picture currently being drawn,
  along with a "Z-Buffer" which contains the depth values for every pixel in
  the image buffer. Once all desired drawing operations for a particular
  picture are effected, a function is provided to print the current contents
  of the image buffer as a PAM image on standard output.  Another function is
  provided to clear the contents of the frame buffer (i. e. set all image
  samples and Z-Buffer entries to 0), with the option of only clearing either
  the image buffer or the Z-Buffer individually.

  The Z-Buffer works as follows: Every pixel in the image buffer has a
  corresponding entry in the Z-Buffer. Initially, every entry in the Z-Buffer
  is set to 0. Every time we desire to plot a pixel at some particular
  position in the frame buffer, the current value of the corresponding entry
  in the Z-Buffer is compared against the the Z component of the incoming
  pixel. If MAX_Z minus the value of the Z component of the incoming pixel is
  equal to or greater than the current value of the corresponding entry in the
  Z-Buffer, the frame buffer is changed as follows:

    1. All the samples but the last of the corresponding position in the
       image buffer are set to equal those of the incoming pixel.

    2. The last sample, that is, the A-component of the corresponding position
       in the image buffer is set to equal the maxval.

    3. The corresponding entry in the Z-Buffer is set to equal MAX_Z minus the
       value of the Z component of the incoming pixel.

    Otherwise, no changes are made on the frame buffer.
-----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
  Set the tuple type for the output PAM images given a string ("str") of 255
  characters or less. If the string has more than 255 characters, the function
  returns 0. Otherwise, it returns 1. If NULL is given for the "str" argument,
  the tuple type is set to a null string. This function is called during
  program initialization and whenever a "r" command is executed. The second
  argument must point to the tuple_type member of the "outpam" field in the
  framebuffer_info struct.
-----------------------------------------------------------------------------*/
int
set_tupletype(const char * str,
              char         tupletype[256]);

int
init_framebuffer(framebuffer_info *);

void
free_framebuffer(framebuffer_info *);

void
print_framebuffer(framebuffer_info *);

void
clear_framebuffer(bool               clear_image_buffer,
                  bool               clear_z_buffer,
                  framebuffer_info *);

/*----------------------------------------------------------------------------
  Reallocate the image buffer with a new maxval and depth, given the struct
  with information about the framebuffer. The fields variables "maxval" and
  "num_attribs".

  From the point this function is called onwards, new PAM images printed on
  standard output will have the new maxval for the maxval and num_attribs + 1
  for the depth.

  This function does *not* check whether the new maxval and num_attribs are
  within the proper allowed limits. That is done inside the input processing
  function "process_next_command", which is the only function that calls this
  one.

  If the function suceeds, the image buffer is left in cleared state. The
  Z-Buffer, however, is not touched at all.

  If the new depth is equal to the previous one, no actual reallocation is
  performed: only the global variable "maxval" is changed. But the image
  buffer is nonetheless left in cleared state regardless.
-----------------------------------------------------------------------------*/
int
realloc_image_buffer(int32_t            new_maxval,
                     int32_t            new_num_attribs,
                     framebuffer_info *);

/*----------------------------------------------------------------------------
  Draw a horizontal span of "length" pixels into the frame buffer, performing
  the appropriate depth tests. "base" must equal the row of the frame buffer
  where one desires to draw the span *times* the image width, plus the column
  of the first pixel in the span.

  This function does not perform any kind of bounds checking.
-----------------------------------------------------------------------------*/
void draw_span(uint32_t           base,
               uint16_t           length,
               fract *            attribs_start,
               const fract *      attribs_steps,
               int32_t            divisor,
               framebuffer_info *);


/*----------------------------------------------------------------------------
   Boundary buffer functions
------------------------------------------------------------------------------
   New triangles are drawn one scanline at a time, and for every such scanline
   we have left and right boundary columns within the frame buffer such that
   the fraction of the triangle's area within that scanline is enclosed
   between those two points (inclusive). Those coordinates may correspond to
   columns outside the frame buffer's actual limits, in which case proper
   post-processing should be made wherever such coordinates are used to
   actually plot anything into the frame buffer.
-----------------------------------------------------------------------------*/

void
init_boundary_buffer(boundary_info * ,
                     int16_t         height);

void
free_boundary_buffer(boundary_info *);

/*----------------------------------------------------------------------------
  Generate an entry in the boundary buffer for the boundaries of every
  VISIBLE row of a particular triangle. In case there is no such row,
  start_row is accordingly set to -1. The argument is a 3-element array
  of pairs of int16_t's representing the coordinates of the vertices of
  a triangle. Those vertices MUST be already sorted in order from the
  uppermost to the lowermost vertex (which is what draw_triangle, the
  only function which uses this one, does with the help of sort3).

  The return value indicates whether the middle vertex is to the left of the
  line connecting the top vertex to the bottom vertex or not.
-----------------------------------------------------------------------------*/

bool
gen_triangle_boundaries(int32_t         xy[3][2],
                        boundary_info *,
                        int16_t         width,
                        int16_t         height);

/*----------------------------------------------------------------------------
  Return the left and right boundaries for a given VISIBLE triangle row (the
  row index is relative to the first visible row). These values may be out of
  the horizontal limits of the frame buffer, which is necessary in order to
  compute correct attribute interpolations.
-----------------------------------------------------------------------------*/
void
get_triangle_boundaries(uint16_t              row_index,
                        int32_t *             left,
                        int32_t *             right,
                        const boundary_info *);

/*----------------------------------------------------------------------------
   Triangle functions
-----------------------------------------------------------------------------*/

void
draw_triangle(int32_t            xy[3][2],
              int32_t            attribs[3][MAX_NUM_ATTRIBS + 1],
              boundary_info *,
              framebuffer_info *);

/*----------------------------------------------------------------------------
   Input handling functions
-----------------------------------------------------------------------------*/

void
init_input_processor(input_info *);

void
free_input_processor(input_info *);

/*----------------------------------------------------------------------------
  Doesn't necessarily process a command, just the next line of input, which
  may be empty. Always returns 1, except when it cannot read any more lines of
  input, an image buffer reallocation fails, or a "q" command is found in the
  input -- in such cases it returns 0.
-----------------------------------------------------------------------------*/
int
process_next_command(input_info *,
                     boundary_info *,
                     framebuffer_info *);
