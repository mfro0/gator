/* 
    This file is part of genericv4l.

    genericv4l is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    genericv4l is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* queue functions used for streaming io (to queue and dequeue buffers) */

#define __NO_VERSION__

#include "generic.h"
#include "queue.h"

void init_frame_queue(FIFO *queue)
{
  int i;
  for (i=0;i<VIDEO_MAX_FRAMES;i++){
    queue->data[i] = -1;
  }
  queue->front = 0;
  queue->back = 0;
}

void add_frame_to_queue(FIFO *queue, int frame)
{
  queue->data[queue->back] = frame;
  queue->back = (queue->back+1) % (VIDEO_MAX_FRAMES+1);
}

int frame_queue_empty(FIFO *queue)
{
  return queue->front == queue->back;
}

int remove_frame_from_queue(FIFO *queue)
{
  int frame;

  if (frame_queue_empty(queue))
    return -1;

  frame = queue->data[queue->front];
  queue->front = (queue->front+1) % (VIDEO_MAX_FRAMES+1);
  return (frame);
}
