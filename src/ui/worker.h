/* ui/worker.h: async worker thread for provider calls.
 * Routes fake_provider/plex calls off the main thread so the UI never
 * blocks >50 ms. While a request is pending, draw ui_draw_spinner().
 */
#ifndef PP_WORKER_H
#define PP_WORKER_H

#include "ui.h"
#include <SDL.h>

typedef enum {
  REQ_NONE,
  REQ_SERVERS,
  REQ_SECTIONS,
  REQ_CHILDREN,
  REQ_ON_DECK,
} pp_req_type;

/* Request context: the UI submits one of these, then polls for done. */
typedef struct {
  pp_req_type type;
  const char *key;          /* for REQ_CHILDREN */
  pp_list  result;          /* filled by worker on completion */
  pp_server *servers;       /* filled by REQ_SERVERS */
  int      server_count;
  int      done;            /* 1 when the worker has finished */
  int      status;          /* 0 = ok, <0 = error */
  char     error[128];
} pp_request;

/* Submit a request to the worker. Returns 0 if accepted (only one
 * in-flight at a time; the previous request must be collected first). */
int  worker_submit(pp_request *req);

/* Returns 1 if the request's worker has finished (req->done == 1). */
int  worker_check(pp_request *req);

/* Draw a simple spinner at (x,y). Call while a request is pending. */
void ui_draw_spinner(int x, int y, int frame);

/* Current spinner animation frame (incremented each render). */
extern int g_spinner_frame;

#endif /* PP_WORKER_H */
