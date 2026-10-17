# The display family

The display service's buffer producer, the binder object behind a layer that a game queues its
frames to, asked for its buffer history: transaction 17 of `IGraphicBufferProducer`
(`GetBufferHistory`), which a game was seen to send once a frame. Only the word `display` runs it
(see [The mode file](../ModeFile.md)), since no console has yet shown what it answers; the start
screen has no row for it, and a file that names it keeps it whatever the screen changes.

- **binder**, four cases. One says a layer of the suite's own opened and took a window (none tier);
  the three after it queue frames to that layer and judge the raw reply to the transaction. If the
  layer does not open, the three are skipped as `depends`.

## How a case runs

The suite opens one layer of its own on the default display (`viOpenDefaultDisplay`,
`viCreateLayer`), makes a window on it (`nwindowCreateFromLayer`) with two buffers of 64 by 64
(`framebufferCreate`), and checks that the window's binder is not the program's: the window the
screens draw into is never touched. Each case queues frames up to its count, dequeuing and queuing
black buffers with `nwindowDequeueBuffer` and `nwindowQueueBuffer` (the library's framebuffer calls
end the program on a failed queue rather than say so), then sends transaction 17 with the
interface token and a count, serialised as libnx's `parcelTransact` serialises a parcel, through
`binderTransactParcel`, and keeps the whole reply parcel. The suite closes the window, the layer
and the display before it returns, so the screen is as it was; while it runs, the layer shows a
black square stretched over the screen for a few frames.

One layer for the three cases, its frames adding up, because asking the console for a managed
layer per case might be refused for reasons that have nothing to do with the history: the request
of nought runs first, before any frame, then three frames, then seven more for ten. The suite runs
after every drawing suite: under Eden a graphics device made after a framebuffer was closed never
finished its first submit, and the suite closes one.

## How a case passes

Two layouts are known, both reden's (see below): the status alone, a word of nought (layout A), and
the history: the status, a count n, n entries of 32 bytes (a frame number, a queue time and a
presentation time of 64 bits, a state of 32 and four bytes of padding) newest first, and the status
again (layout B). A request of nought or less is refused under B with BadValue (-22) and a count of
nought, then the status. A case passes when the reply fits one of them as it should: the history
with as many entries as frames were queued, at most eight asked for, each frame number below the
one before. The line always carries the reply, its header and every byte of the payload, in hex:
what Q1 needs, and what a reply in neither layout fails with.

```text
the status alone (Q1 A), no frames to count or order; reply: payload 4 bytes at 16, objects 0 at 20: 00000000
3 entries of 3 (Q1 B), frames 3 down to 1, newest first; reply: payload 108 bytes at 16, objects 0 at 124: 00000000 03000000 03000000 00000000 b80b0000 00000000 00000000 00000000 02000000 00000000 02000000 00000000 d0070000 00000000 00000000 00000000 02000000 00000000 01000000 00000000 e8030000 00000000 00000000 00000000 02000000 00000000 00000000
refused with BadValue and a count of nought (Q1 B); reply: payload 12 bytes at 16, objects 0 at 28: eaffffff 00000000 00000000
```

The lines above are what the judge says of replies made to order: reden by default, reden with
`enable_buffer_history` after three frames, and its refusal of a request of nought. One note after
the suite says which layout the passing cases fit:

```json
{"type":"note","text":"binder: the passing cases answer Q1 A"}
```

## The cases

| case | frames queued | asked for | passes when | tier |
|---|---|---|---|---|
| a layer of its own opens and takes a window | | | the layer, window and buffers open, on a binder not the program's | none |
| a request for nought, before any frame | 0 | 0 | the status alone, or BadValue, nought and the status | exact |
| the history after three frames, eight asked for | 3 | 8 | the status alone, or a history of 3 newest first | exact |
| the history after ten frames comes newest first, eight at most | 10 | 8 | the status alone, or a history of 8 newest first | position |

## The open question

- **Q1.** The console's reply: A the status alone, B reden's history, or neither, which fails the
  cases and leaves its bytes in their lines. Two details of B are worth the bytes: the status at the
  start and again at the end, which looks doubled, and the size of each entry and its state field.

## What Eden does today

Read in reden at 75c12299a6, `src/core/hle/service/nvnflinger/` unless said otherwise.

- **G1, the status alone by default.** `buffer_queue_producer.cpp:895-899`: with
  `enable_buffer_history` off (the default, `src/common/settings.h:608`) the case does nothing but
  `break`, and the end of the function writes the status (`:936`; the handback said `:935`, the
  line after the switch's closing brace): a reply of one word, nought. Every case passes, Q1 A.
- **G2, the history with the option on.** `:901-929`: the status, `limit`, the smaller of the
  request and what is kept, `limit` entries (`BufferHistoryInfo`, `buffer_queue_core.h:31-36`, 32
  bytes with the padding) newest first, and the status again at `:936`. It keeps the last eight
  frames (`BUFFER_HISTORY_SIZE`, `buffer_queue_core.h:47`, dropped at `buffer_queue_core.cpp:37`),
  each pushed when it is queued, with the state Queued (`buffer_queue_producer.cpp:532-534`). Every
  case passes, Q1 B.
- **G3, a request of nought.** `:904-907` under the option: BadValue and a count of nought, then the
  status at `:936`.

The layer opens: each managed layer asked for is a new one
(`service/am/display_layer_manager.cpp:69-95`), and closing it takes it off the display stack
(`service/vi/container.cpp:259-273`), so the screen is as it was once the suite is over.

## The judge, pinned

The `reference` suite holds `source/binderref.c` on reply parcels built word by word, three cases
in both builds: both of reden's layouts pass, a history oldest first, longer than asked or shorter
than queued fails, a reply in neither layout or with a header pointing outside it fails with its
bytes, and the cases' frames, requests and order. Mutations of the newest-first check, the status
word, the entry count, BadValue for a request of eight and the bytes on the line each fail it.
