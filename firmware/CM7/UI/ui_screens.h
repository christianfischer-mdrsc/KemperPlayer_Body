/**
 * @file ui_screens.h
 * Builder der einzelnen Ansichten (nur intern fuer ui_common.c).
 */
#ifndef UI_SCREENS_H
#define UI_SCREENS_H

#include "ui_common.h"

lv_obj_t * ui_live_build(void);      /* ui_live.c     */
lv_obj_t * ui_edit_build(void);      /* ui_edit.c     */
lv_obj_t * ui_banks_build(void);     /* ui_banks.c    */
lv_obj_t * ui_tuner_build(void);     /* ui_tuner.c    */
lv_obj_t * ui_settings_build(void);  /* ui_settings.c */

#endif /* UI_SCREENS_H */
