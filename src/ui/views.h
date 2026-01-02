#ifndef VIEWS_H
#define VIEWS_H

#include <lvgl.h>

/**
 * Available views
 */
typedef enum {
    VIEW_CLOCK,
    VIEW_THERMOSTAT,
    VIEW_COUNT  // Keep last - total number of views
} ViewType;

/**
 * Initialize the view manager
 * Creates all view containers
 */
void views_init();

/**
 * Switch to a specific view
 * @param view The view to switch to
 */
void views_switch(ViewType view);

/**
 * Toggle to the next view
 */
void views_next();

/**
 * Get the current active view
 * @return Current view type
 */
ViewType views_current();

/**
 * Update views (call in main loop)
 * Handles view-specific updates like clock time
 */
void views_update();

#endif // VIEWS_H
