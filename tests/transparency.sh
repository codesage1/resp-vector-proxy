#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

echo "Starting Transparency Battery..."

# 1. Define the exact same payload for both runs using a Here-Doc
PAYLOAD=$(cat <<EOF
FLUSHALL
PING
GET this_key_does_not_exist
SET name harsh
GET name
INCR mycounter
LPUSH mylist a b c
LRANGE mylist 0 -1
COMMAND DOCS
EOF
)

# 2. Fire the payload at the REAL Redis (Port 6379) and save the output
echo "Routing to Real Redis (6379)..."
echo "$PAYLOAD" | redis-cli -p 6379 > tests/expected.txt

# 3. Fire the payload at OUR Proxy (Port 7000) and save the output
echo "Routing to Proxy (7000)..."
echo "$PAYLOAD" | redis-cli -p 7000 > tests/actual.txt


echo "Comparing outputs..."
if diff tests/expected.txt tests/actual.txt > /dev/null; then
    echo "PASS: Zero diff! The proxy is perfectly transparent."
    rm tests/expected.txt tests/actual.txt
else
    echo "❌ FAIL: The outputs do not match!"
    diff tests/expected.txt tests/actual.txt
    exit 1
fi