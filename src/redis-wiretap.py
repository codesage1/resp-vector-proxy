import numpy as np
from redis import Redis

# 1. Connect to the socat wiretap (which forwards to 6379)
client = Redis(host='localhost', port=7001)
client.flushall()

client.execute_command(
    "FT.CREATE", "idx", 
    "ON", "HASH", 
    "PREFIX", "1", "docs:", 
    "SCHEMA", "doc_embedding", "VECTOR", "FLAT", "6", 
    "TYPE", "FLOAT32", 
    "DIM", "2", 
    "DISTANCE_METRIC", "COSINE"
)
# Create a FLOAT32 vector
v1 = np.array([4.0, 0.0], dtype=np.float32).tobytes()
client.hset("docs:1", mapping={"doc_embedding": v1, "category": "test"})

v2 = np.array([1.0, 1.0], dtype=np.float32).tobytes()
client.hset("docs:2", mapping={"doc_embedding": v2, "category": "test"})

v3 = np.array([0.0, 4.0], dtype=np.float32).tobytes()
client.hset("docs:3", mapping={"doc_embedding": v3, "category": "test"})


# 4. Fire the FT.SEARCH with your Query Vector (Q)
q_vec = np.array([1.0, 0.0], dtype=np.float32).tobytes()

# 5. Execute the search and ask for the score to be returned
client.execute_command(
    "FT.SEARCH", "idx", 
    "*=>[KNN 3 @doc_embedding $vec AS score]", 
    "PARAMS", "2", "vec", q_vec, 
    "DIALECT", "2"
)

print("✅Script Executed.")

#the wiretrap
#socat -v -x TCP-LISTEN:7001,fork,reuseaddr TCP:127.0.0.1:6379