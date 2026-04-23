#include <stdint.h>

#include "dhry_result.h"
#include "third_party/dhry.h"

extern int main(void);

extern Rec_Pointer Ptr_Glob;
extern Rec_Pointer Next_Ptr_Glob;
extern int Int_Glob;
extern Boolean Bool_Glob;
extern char Ch_1_Glob;
extern char Ch_2_Glob;
extern int Arr_1_Glob[50];
extern int Arr_2_Glob[50][50];

static volatile dhry_result_t *const dhry_result =
  (volatile dhry_result_t *)DHRY_RESULT_ADDR;

static uint32_t dhry_status(void)
{
  if (Int_Glob != 5)
    return DHRY_STATUS_FAIL;
  if (Bool_Glob != 1)
    return DHRY_STATUS_FAIL;
  if (Ch_1_Glob != 'A' || Ch_2_Glob != 'B')
    return DHRY_STATUS_FAIL;
  if (Arr_1_Glob[8] != 7)
    return DHRY_STATUS_FAIL;
  if (Arr_2_Glob[8][7] != (DHRY_ITERS + 10))
    return DHRY_STATUS_FAIL;
  if (!Ptr_Glob || !Next_Ptr_Glob)
    return DHRY_STATUS_FAIL;
  if (Ptr_Glob->variant.var_1.Int_Comp != 17)
    return DHRY_STATUS_FAIL;
  if (Next_Ptr_Glob->variant.var_1.Int_Comp != 18)
    return DHRY_STATUS_FAIL;

  return DHRY_STATUS_PASS;
}

static void write_result(void)
{
  dhry_result->magic = 0;
  dhry_result->status = dhry_status();
  dhry_result->iterations = DHRY_ITERS;
  dhry_result->int_glob = Int_Glob;
  dhry_result->bool_glob = Bool_Glob;
  dhry_result->ch_1_glob = (uint32_t)(unsigned char)Ch_1_Glob;
  dhry_result->ch_2_glob = (uint32_t)(unsigned char)Ch_2_Glob;
  dhry_result->arr_1_8 = Arr_1_Glob[8];
  dhry_result->arr_2_8_7 = Arr_2_Glob[8][7];
  dhry_result->ptr_int_comp =
    Ptr_Glob ? Ptr_Glob->variant.var_1.Int_Comp : -1;
  dhry_result->next_ptr_int_comp =
    Next_Ptr_Glob ? Next_Ptr_Glob->variant.var_1.Int_Comp : -1;
  dhry_result->magic = DHRY_RESULT_MAGIC;
}

void c_start(void)
{
  main();
  write_result();

  for (;;)
    ;
}
