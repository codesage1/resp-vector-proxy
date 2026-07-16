#include <math.h>
#include <string.h>

// Note: 'a' and 'b' are the raw wire bytes. 'dim' is the number of dimensions (e.g., 1536).
double dist_cosine(const unsigned char *a, const unsigned char *b, long long dim) {
    double dot_product = 0.0;
    double norm_a = 0.0;
    double norm_b = 0.0;

    for (long long i = 0; i < dim; i++) {
        float val_a, val_b;
        
        // Safely extract exactly 4 bytes into a perfectly aligned stack variable
        // We offset the pointer by (i * 4) to grab the correct float block
        memcpy(&val_a, a + (i * sizeof(float)), sizeof(float));
        memcpy(&val_b, b + (i * sizeof(float)), sizeof(float));

        // Use doubles for the accumulation to prevent precision loss
        dot_product += (double)val_a * (double)val_b;
        norm_a += (double)val_a * (double)val_a;
        norm_b += (double)val_b * (double)val_b;
    }

    // The Zero-Magnitude Decision
    if (norm_a == 0.0 || norm_b == 0.0) {
        // If a vector has 0 magnitude, it has no direction. 
        // Max distance (1.0) or (2.0) is mathematically appropriate.
        return 2.0; 
    }

    double similarity = dot_product / (sqrt(norm_a) * sqrt(norm_b));
    
    // Float math can sometimes return 1.000000001, pushing distance slightly negative.
    // Clamp it to 0 at the floor.
    double distance = 1.0 - similarity;
    return (distance < 0.0) ? 0.0 : distance;
}