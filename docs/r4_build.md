
# Design Questions: Architecture & Trade-offs

## Q1: The Data Question

**Problem:** The real  architecture intercepts `FT.SEARCH` queries and routes them to Oracle, but the client writes data (`HSET`) to Kvrocks. How does Oracle acquire the data to answer the search?

**Candidate Architectures & Failure Modes:**

1. **Client Dual-Writes:** The client application is explicitly configured to send writes to both Kvrocks and Oracle.
* *Failure Mode (Client Rebellion & Fragility):* This destroys the abstraction layer. If the client crashes between the two writes, the databases instantly desync, and the client team shoulders the burden of distributed transaction management.


2. **Proxy Dual-Writes:** The interception proxy receives the `HSET`, forwards it to Kvrocks, and simultaneously writes it to Oracle before acknowledging the client.
* *Failure Mode (Split-Brain & Partial Writes):* If the proxy successfully writes to Kvrocks but the connection to Oracle drops, what does the proxy return to the client? Returning `+OK` leaves Oracle permanently missing data; returning `-ERR` makes the client think the write failed entirely when it actually sits in Kvrocks. It causes an irreconcilable conflicting truth.


3. **Background Sync / CDC (Change Data Capture):** Kvrocks acts as the primary system of record. A background process tails the transaction log and asynchronously streams updates into Oracle.
* *Failure Mode (Replication Lag):* Because the sync is asynchronous, a client might execute an `HSET` and immediately fire an `FT.SEARCH` for that same vector. If the CDC pipeline takes 50ms, Oracle will return zero results for data that the client knows it just wrote.



## Q2: The Score Question

**Problem:** Determining score semantics (similarity vs. distance) and defining architectural boundaries for mathematical conversions.

* **Ground Truth Analysis:** When querying Redis Stack using a `COSINE` metric, the score returned for identical vectors is `0`. Therefore, Redis returns a **Distance** metric ($1 - \text{Cosine Similarity}$). Oracle's function, `VECTOR_DISTANCE(v1, v2, COSINE)`, natively returns the same quantity (Distance).
* **Conversion Placement:** If a conversion were ever required between similarity and distance, this logic strictly belongs in the **SQL query layer** or the **C glue (controller)**. It must *never* be handled by the reply formatter. The serialization layer is responsible solely for encoding C data types into RESP bytes; leaking business logic or floating-point math into the formatter violates the Single Responsibility Principle and creates brittle code.

## Q3: The Consistency Question

**Problem:** Client A writes a document via `HSET`. Client B immediately queries for it via `FT.SEARCH`. Because writes route to Kvrocks and reads route to Oracle via a background sync, the query returns zero results.

* **The Broken Guarantee:** The architecture permanently sacrifices **Read-After-Write Consistency** (Read-Your-Writes). Because of the physical replication lag between the system of record and the vector engine, clients cannot trust that their own writes are instantly searchable.
* **The Reality:** The system operates under **Eventual Consistency**. Provided the clients wait out the replication lag, the data will eventually be indexed and queryable in Oracle.

## Q4: The Bootstrap Question

**Problem:** The proxy's in-memory schema registry populates by actively watching `FT.CREATE` commands. It is completely blind to any indexes that were created before the proxy was started.

**Discovery Mechanisms:**
To solve this cold-start blindness, an interceptor could utilize native Redis introspection commands:

1. **Active Startup Introspection:** Upon booting, the proxy issues an `FT._LIST` command to the backend to retrieve all existing indexes, followed by a loop of `FT.INFO <index>` to proactively rebuild its internal schema registry before opening the port to client traffic.
2. **Lazy Loading (Just-In-Time):** If the proxy intercepts an `FT.SEARCH` for an unknown index, it pauses the client request, fires a synchronous `FT.INFO` to the backend to discover the schema on the fly, caches it in the registry, and then processes the query.

> **Note:** For the scope of the v1-interceptor, these mechanisms are omitted. Pre-existing indexes represent a known limitation and will currently bypass the proxy and fail open to the backend.









Solid — these are real answers, in your voice, and you've correctly identified the highest-leverage question on your entire ladder. Grading with a hard pen, because a soft one wouldn't serve you.

**Q1 (data question) — you've reasoned your way to the right shortlist, but under-priced the winner and over-priced the loser.**

Two things to reconsider. First, "no client would want dual writes — extra work, risk" — that's your intuition, and it's the intuition of every engineer meeting this problem for the first time. But it's not universally true, and knowing why makes you sound senior on day one. Dual writes at the *client* are the pattern behind write-through caches, and they're occasionally correct — when the two stores have different roles (one authoritative, one for lookup), when the client can honestly commit to both, and when application logic already handles the failure. What kills it in *our* case is a different reason:  whole product promise is *transparent* — the client can't be asked to know about Oracle, because then Oracle stops being an implementation detail and becomes an API. So dual-write-at-client is rejected on **transparency grounds**, not just "extra work." That's a sharper argument, and it's the one that survives contact with someone who's built a cache before.

Second — and this is the one — you correctly named partial writes for proxy-side dual-write, and correctly named replication lag for background sync. Now grade them honestly. Partial writes are a **correctness** failure: after a crash, the two stores disagree, and no future read fixes that without operator intervention. Replication lag is a **freshness** failure: the two stores briefly disagree, and time alone fixes it. Correctness bugs are unbounded; freshness bugs are bounded. That asymmetry is why virtually every production two-store system on Earth picks eventual consistency plus a log — Kafka, Debezium, MySQL→Elasticsearch, this exact shape shipped a thousand times. So "background sync (best one)" is right *in ranking*, but I want you to write down the reason as a principle: **prefer bounded staleness over unbounded divergence.** That sentence is worth memorizing.

One more distinction to notice, because it's Rung 4's design lurking in your answer: your shadow store in Rung 4 *is* the local proxy-side version of "background sync," except the log is the client's own commands passing through your interceptor. Every architecture on your shortlist is really the same question: *who tails the log, and where does the mirror live?* Say that out loud once and the whole problem gets smaller.

**On your OTP question — this is the exact right instinct, and the answer is a real technique with a name.** Eventual consistency doesn't mean "we shrug at OTPs." It means the *architecture* is eventually consistent and you build **read-your-writes consistency** on top for the operations that need it. Techniques, so you have vocabulary: read from the primary (Redis) when the request has "just-written" context, and only fall through to the secondary when safe — session stickiness, causality tokens, sometimes a monotonic version number the client passes back. Redis + a slow warehouse is a canonical pairing; nobody sends OTPs from the warehouse. The system doesn't remove the tradeoff — it *routes around it per query*. This is a **superb** day-one question: "fo, what's the read-consistency contract per command class, and how do we route reads when clients need read-your-writes?" Write it down.

**Q2 (score question) — right conclusion, half-argued.** "C glue is cleaner" — I agree, but *why*? Because SQL is a communication protocol, C is where our behavior lives; putting a semantic conversion in the SQL means the *database* is now responsible for the wire's meaning, which is upside-down. The rule to internalize: **transformations belong on the side that owns the wire.** Same rule, incidentally, that says the client shouldn't dual-write.

**Q3 (consistency) — you named the problem well; you missed answering the question I actually asked.** The spec asked why your *one-client* proxy is currently safe. The answer is that a single connection with sequential syscalls gives you total ordering — HSET completes end-to-end before FT.SEARCH is even read off the wire, so read-your-writes is free. The moment two clients share a proxy (Rung 4's Q3), that ordering disappears: the proxy has no locks, no versioning, no serialization guarantee between A's HSET and B's FT.SEARCH. What just quietly died is a specific guarantee with a specific name — **linearizability** — replaced by "whatever poll() woke up first." Say those two sentences and you sound like someone who's read a distributed systems paper, not just felt the pain.

**Q4 (bootstrap) — this one is genuinely good.** Both mechanisms named correctly, `FT._LIST` + `FT.INFO` is exactly right, and you spontaneously identified lazy-load as the scaling escape hatch — that's real design instinct. Two refinements. First, mechanisms 1 and 2 aren't either/or; production systems do both — eager warm-up for known hot indexes, lazy for the long tail. Second, both mechanisms share one silent assumption: **the registry is a snapshot, not a subscription.** What happens when a *different* client, on a *different* proxy instance, creates a new index? Your proxy doesn't know. That's a whole class of distributed-registry problems (invalidation, gossip, watch APIs) which is another beautiful day-one question, gift-wrapped by your own answer.

**Curate your day-one list now**, don't wait for the capstone. From these answers alone you've minted three genuinely sharp ones — the transparency argument against client dual-writes, per-command read-consistency contracts, and cross-instance registry invalidation. Add them to `docs/day-one.md` today so they don't evaporate.

Two thinking habits to notice, because they'll compound: you're now instinctively separating "why is this bad" from "why is this bad *for our specific product*," and you're spotting failure modes before I have to name them. That's the transition from student to engineer. Keep going.

Now go build Rung 4 — but the gate stands: **four closed-book answers first**, the ones from the last review (size_t underflow, discarded is_knn return, stranded knn_parse, forward-vs-reserialize). Those close Rung 3. Then M1.



This is the exact moment where the "intern" training wheels come off and the "systems engineer" mindset kicks in. Your mentor is throwing heavy, production-grade distributed systems theory at you.

Let’s break down exactly what your mentor meant, piece by piece, and then we will clean up your Rung 3 answers.

---

### Part 1: "Total Ordering" vs. "Linearizability" (The Q3 Explanation)

Your mentor asked why your proxy currently gives perfect "Read-Your-Writes" consistency, but will break when a second client connects.

**The One-Client Safe Zone (Total Ordering):**
Right now, you have one client. The operating system handles the TCP stream sequentially.

1. The proxy reads the socket. It sees `HSET`.
2. The proxy pauses, forwards it to Redis, waits for the `+OK`.
3. The proxy sends `+OK` to the client.
4. *Only then* does the proxy read the socket again and see `FT.SEARCH`.
Because everything happens on a single, strict timeline governed by the C code waiting on a single socket, you have **Total Ordering**. Everything happens exactly in the order the client sent it.

**The Multi-Client Chaos (Linearizability Dies):**
Now add Client B. Your proxy is using a function called `poll()` to watch multiple sockets at once.

1. Client A sends an `HSET` on Socket 4.
2. At the *exact same physical millisecond*, Client B sends an `FT.SEARCH` on Socket 5.
3. Your proxy calls `poll()`. The operating system says, "Hey, Sockets 4 and 5 both have data ready!"
4. Your C code loops through the ready sockets. Which one does it process first? Usually, whichever one has the lower file descriptor number, or whichever one the OS scheduled first. It is entirely arbitrary.

**Linearizability** is the distributed systems guarantee that operations appear to execute instantaneously at a specific point in global time. Because your proxy doesn't have a global clock, no locking mechanism, and no version numbers, it has no idea if the `HSET` was "supposed" to happen before the `FT.SEARCH`. It just processes whichever socket `poll()` handed it first. The guarantee dies.

---

### Part 2: "The Log" and "Bounded Staleness"

Your mentor dropped a massive industry principle here: **Prefer bounded staleness over unbounded divergence.** Let's decode this.

**What is "The Log"?**
In databases, a "Log" (specifically a Write-Ahead Log or Transaction Log) is an append-only file. Every time you write data to a database (like MySQL or Redis), before it even updates the tables, it writes the command to the end of a text file: `1. INSERT X, 2. UPDATE Y, 3. DELETE Z`.

**Why does Kafka/Debezium use this?**
Imagine you have MySQL (System of Record) and Elasticsearch (Search Engine). You need them to have the same data.

* **The Bad Way (Unbounded Divergence):** Your API tries to write to both. MySQL succeeds. The network glitches, and the Elasticsearch write fails. You now have a **correctness bug**. The databases are out of sync *forever* unless a human fixes it. The divergence has no bounds.
* **The Good Way (Bounded Staleness + The Log):** Your API *only* writes to MySQL. Debezium is a tool that sits there and literally reads the MySQL Log file line-by-line. When it sees `INSERT X`, it copies it to Kafka. Another tool reads Kafka and pushes it to Elasticsearch.

**What happens if Elasticsearch crashes in the Good Way?**
The log reader just pauses. It remembers it left off at line 4,000. MySQL keeps taking writes (lines 4001, 4002...). Elasticsearch is now out of date (**stale**). But the moment Elasticsearch boots back up, the reader picks up at line 4001 and catches up. The staleness is **bounded** by time. Time heals the system automatically.

**Your Proxy is a Log Reader:**
When your mentor says, *"your shadow store in Rung 4 is the local proxy-side version of 'background sync', except the log is the client's own commands,"* they mean that your proxy is literally sitting on the wire, reading the stream of TCP packets as if it were an append-only log, and updating your in-memory C registry based on what it observes.

---

### Part 3: Grading Your Rung 3 Closed-Book Answers

You got the concepts mostly right, but we need to tighten the C-level engineering vocabulary before you close the book on Rung 3.

**1. `size_t` underflow**

* **Your Answer:** *it will go the -ve,, hence underflow*
* **The Engineer's Answer:** Yes, but `size_t` is an *unsigned* integer. It physically cannot be negative. If `size_t len = 2;` and your loop does `i < len - 3`, `2 - 3` wraps backward around zero and becomes the maximum 64-bit integer (`18,446,744,073,709,551,615`). Your loop will attempt to read memory into infinity until the operating system throws a Segmentation Fault.

**2. Discarded function return value (`is_knn`)**

* **Your Answer:** *removed that func, were not using its value, so no problem*
* **The Engineer's Answer:** The problem is **wasted CPU cycles (Latency)**. By calling `is_knn` but doing nothing with the 1 or 0 it returned, the proxy performed an expensive sliding-window string search across the payload, burned CPU time, and immediately threw the result in the garbage. In a proxy handling 100,000 requests per second, dead code kills throughput.

**3. The stranded pointer**

* **Your Answer:** *we need to free it, otherwise memory will be in use*
* **The Engineer's Answer:** No! Remember our "Zero-Copy" architecture? We never called `malloc()`, so we **never call `free()**`. If we call `free()`, we will crash the proxy. A stranded pointer happens if `knn_parse` finds the `"vec"` string, saves the memory address in a local variable, but forgets to execute `*out_param_name = param_name`. The data isn't leaked on the heap; the *information* is leaked. The function did the work, but failed to hand the coordinates back to `main()`.

**4. Raw forwarding vs. unpacking/reserializing**

* **Your Answer:** *please explain , no idea*
* **The Engineer's Answer:** * *Raw Forwarding:* When a normal command (like `PING`) arrives, you just take the raw bytes from the client socket and `write()` them directly to the Redis socket. It costs zero memory and almost zero CPU.
* *Reserializing:* If you unpack a command, you run it through your parser, allocate `resp_value` structs, build an Abstract Syntax Tree, and then run it through a formatter to turn it *back* into bytes. This requires CPU processing, heap memory allocation, and risks altering the protocol formatting by mistake. We only reserialize when we are *forging* a reply (like your vector search). Otherwise, we raw-forward.



Update your notes with those refined definitions. Once those four are locked in your head, Rung 3 is officially dead. Are you ready to fire up `socat` and capture your M1 Ground Truth bytes?