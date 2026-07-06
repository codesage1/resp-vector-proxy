DESIGN QUESTIONS-

1) The deadlock question. The obvious loop is:
read(client) → write(upstream) → read(upstream) → write(client) → repeat.
Describe a specific, real traffic pattern where this hangs forever with
both sides waiting. (You have personally already generated such traffic.
Check your wire.log. Hint: 227,583 bytes, delivered 8192 at a time.)

ANS - If Upstream sends a massive 227KB reply, the proxy reads the first 8KB chunk and writes it to the client. But the naive loop then forces the proxy to call read(client). Because read() blocks, the proxy goes to sleep waiting for the client to send a new command. The client won't send anything because it is still waiting for the rest of the 227KB reply. Upstream is blocked trying to send the rest of the reply. The proxy is permanently stuck sleeping on read(client)

2)The short-write question. Mid-shuttle, you ask write() to send 10,000
bytes and it returns 3,000. What must your code do with the remaining
7,000, and — the part that matters — what happens to the byte order of
the stream if you get this wrong and just move on? What does the client's
parser see?

ANS - If you ignore the short-write and move on, you permanently drop 7,000 bytes. The next time you write to the client, those new bytes will be glued directly to the first 3,000 bytes. The client's RESP parser expects strict formatting (like $10000\r\n). Because a massive chunk of the payload is missing, the parser will read random payload bytes thinking they are protocol boundaries, instantly triggering a RESP_PROTO_ERR and completely desyncing the TCP stream.


3)The Rung 3 Question: This rung copies bytes blindly. Rung 3 must find command boundaries. Where exactly in your shuttle loop will the parser hook in, and why must the inspection buffer accumulate bytes across multiple read() calls instead of parsing each read()'s buffer independently? (Connect this to your Rung 1 NEED_MORE contract).

The parser must hook in right after read(client). We must accumulate bytes across multiple reads because TCP does not respect RESP command boundaries; a single read() might only capture half of a command (e.g., +PI). If we parse buffers independently, the first half is lost, and the second half (NG\r\n) causes a protocol error. We must leave the unparsed bytes in the buffer and append new read() data to the end of it until resp_parse finally finds a full boundary and returns RESP_OK