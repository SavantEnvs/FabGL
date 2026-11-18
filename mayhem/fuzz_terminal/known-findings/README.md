# fuzz_terminal: known findings (upstream bugs, reproducers kept out of the seed corpus)

Each subdirectory holds `repro`, the input that triggers it. Replay with
`/mayhem/fuzz_terminal-standalone mayhem/fuzz_terminal/known-findings/<name>/repro` inside the commit image.
The first byte of every input selects the terminal personality (`TermType`); the rest is the byte stream.

| dir | where | cause | fix |
| --- | --- | --- | --- |
| `usrseq-stack-overflow` | `terminal.cpp` `consumeFabGLSeq`, `FABGLEXTB_USERSEQUENCE` (`ESC _ #`) | `usrseq[FABGLEXT_MAXSUBCMDLEN]` is filled while `count < MAXSUBCMDLEN`, then `usrseq[count] = 0` writes element 16: one-byte stack overflow | size the buffer `FABGLEXT_MAXSUBCMDLEN + 1` |
| `decgraph-table-oob` | `terminal.cpp` `consumeInputQueue`, DEC graphics charset | `DECGRAPH_TO_CP437` has 255 entries but is indexed by a full byte (0..255): reads element 255 | declare it with 256 entries |
| `multiline-delete-char-heap-overflow` | `Terminal::multilineDeleteChar`, `ESC _ f` | on the last row the last-column fix-up reads `lastItem[1]`, one glyph past the end of the glyph map | skip the copy when `row` is the last row |
| `multiline-insert-char-heap-overflow` | `Terminal::multilineInsertChar`, `ESC _ e` | the multi-line loop advances `row` beyond `m_rows` and indexes the glyph map past its end | stop at the last row |
| `insert-at-heap-overflow` | `Terminal::insertAt`, CSI `@` (ICH) | with `count` clamped to the column count but `column` larger than the line, the move/fill loops index outside the row | clamp `column` and `count` so `column + count - 1 <= m_columns` |
| `csi-param-signed-overflow` | `consumeCSI`/DCS parameter parsing, `*p = *p * 10 + digit` | unbounded digit run overflows `int` (UBSan; undefined behavior, the value is then used as a count/position) | saturate the accumulator |
| `ext-param-signed-overflow` | `extGetIntParam`, `val = val * 10 + c - '0'` | same overflow for FabGL extension numeric parameters | saturate the accumulator |

Related upstream fixes mentioned in the intake: 2bb8458da1 ("Terminal, bug fix: stack corruption", CSI parameter
overflow) is the same class as the two overflows above.
