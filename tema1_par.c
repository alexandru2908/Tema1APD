// Author: APD team, except where source was noted

#include "helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#define CONTOUR_CONFIG_COUNT 16
#define FILENAME_MAX_SIZE 50
#define STEP 8
#define SIGMA 200
#define RESCALE_X 2048
#define RESCALE_Y 2048

#define CLAMP(v, min, max) \
    if (v < min)           \
    {                      \
        v = min;           \
    }                      \
    else if (v > max)      \
    {                      \
        v = max;           \
    }

typedef struct image
{
    int thread_id;
    ppm_image *image;
    ppm_image *scaled_image;
    unsigned char **grid;
    ppm_image **contour_map;
    pthread_barrier_t *barrier;
    int N;
} image;

// Creates a map between the binary configuration (e.g. 0110_2) and the corresponding pixels
// that need to be set on the output image. An array is used for this map since the keys are
// binary numbers in 0-15. Contour images are located in the './contours' directory.
ppm_image **init_contour_map()
{
    ppm_image **map = (ppm_image **)malloc(CONTOUR_CONFIG_COUNT * sizeof(ppm_image *));
    if (!map)
    {
        fprintf(stderr, "Unable to allocate memory\n");
        exit(1);
    }

    for (int i = 0; i < CONTOUR_CONFIG_COUNT; i++)
    {
        char filename[FILENAME_MAX_SIZE];
        sprintf(filename, "./contours/%d.ppm", i);
        map[i] = read_ppm(filename);
    }

    return map;
}

// Updates a particular section of an image with the corresponding contour pixels.
// Used to create the complete contour image.
void update_image(ppm_image *image, ppm_image *contour, int x, int y)
{
    for (int i = 0; i < contour->x; i++)
    {
        for (int j = 0; j < contour->y; j++)
        {
            int contour_pixel_index = contour->x * i + j;
            int image_pixel_index = (x + i) * image->y + y + j;

            image->data[image_pixel_index].red = contour->data[contour_pixel_index].red;
            image->data[image_pixel_index].green = contour->data[contour_pixel_index].green;
            image->data[image_pixel_index].blue = contour->data[contour_pixel_index].blue;
        }
    }
}

// Corresponds to step 1 of the marching squares algorithm, which focuses on sampling the image.
// Builds a p x q grid of points with values which can be either 0 or 1, depending on how the
// pixel values compare to the `sigma` reference value. The points are taken at equal distances
// in the original image, based on the `step_x` and `step_y` arguments.
unsigned char **sample_grid(ppm_image *image, unsigned char **grid, int thread_id, int N)
{
    int p = image->x / STEP;
    int q = image->y / STEP;

    int start = thread_id * (p / N);
    int end = (thread_id + 1) * (p / N);

    for (int i = start; i < end; i++)
    {
        for (int j = 0; j < q; j++)
        {
            ppm_pixel curr_pixel = image->data[i * STEP * image->y + j * STEP];

            unsigned char curr_color = (curr_pixel.red + curr_pixel.green + curr_pixel.blue) / 3;

            if (curr_color > SIGMA)
            {
                grid[i][j] = 0;
            }
            else
            {
                grid[i][j] = 1;
            }
        }
    }
    grid[p][q] = 0;

    // last sample points have no neighbors below / to the right, so we use pixels on the
    // last row / column of the input image for them

    int start1 = thread_id * (p / N);
    int end1 = (thread_id + 1) * (p / N);

    for (int i = start1; i < end1; i++)
    {
        ppm_pixel curr_pixel = image->data[i * STEP * image->y + image->x - 1];

        unsigned char curr_color = (curr_pixel.red + curr_pixel.green + curr_pixel.blue) / 3;

        if (curr_color > SIGMA)
        {
            grid[i][q] = 0;
        }
        else
        {
            grid[i][q] = 1;
        }
    }

    int start2 = thread_id * (q / N);
    int end2 = (thread_id + 1) * (q / N);

    for (int j = start2; j < end2; j++)
    {
        ppm_pixel curr_pixel = image->data[(image->x - 1) * image->y + j * STEP];

        unsigned char curr_color = (curr_pixel.red + curr_pixel.green + curr_pixel.blue) / 3;

        if (curr_color > SIGMA)
        {
            grid[p][j] = 0;
        }
        else
        {
            grid[p][j] = 1;
        }
    }

    return grid;
}

// Corresponds to step 2 of the marching squares algorithm, which focuses on identifying the
// type of contour which corresponds to each subgrid. It determines the binary value of each
// sample fragment of the original image and replaces the pixels in the original image with
// the pixels of the corresponding contour image accordingly.
void march(ppm_image *image, unsigned char **grid, ppm_image **contour_map, int thread_id, int N)
{
    int p = image->x / STEP;
    int q = image->y / STEP;

    int start = thread_id * (p / N);
    int end = (thread_id + 1) * (p / N);

    for (int i = start; i < end; i++)
    {
        for (int j = 0; j < q; j++)
        {
            unsigned char k = 8 * grid[i][j] + 4 * grid[i][j + 1] + 2 * grid[i + 1][j + 1] + 1 * grid[i + 1][j];
            update_image(image, contour_map[k], i * STEP, j * STEP);
        }
    }
}

// Calls `free` method on the utilized resources.
void free_resources(ppm_image *image, ppm_image **contour_map, unsigned char **grid, int step_x)
{
    for (int i = 0; i < CONTOUR_CONFIG_COUNT; i++)
    {
        free(contour_map[i]->data);
        free(contour_map[i]);
    }
    free(contour_map);

    for (int i = 0; i <= image->x / step_x; i++)
    {
        free(grid[i]);
    }
    free(grid);

    free(image->data);
    free(image);
}

ppm_image *rescale_image(struct image *imagine)
{
    uint8_t sample[3];

    ppm_image *image = imagine->image;
    ppm_image *new_image = imagine->scaled_image;

    // use bicubic interpolation for scaling
    int start = imagine->thread_id * (new_image->x / imagine->N);
    int end = (imagine->thread_id + 1) * (new_image->x / imagine->N);

    for (int i = start; i < end && i < new_image->x; i++)
    {
        for (int j = 0; j < new_image->y; j++)
        {
            float u = (float)i / (float)(new_image->x - 1);
            float v = (float)j / (float)(new_image->y - 1);
            sample_bicubic(image, u, v, sample);

            new_image->data[i * new_image->y + j].red = sample[0];
            new_image->data[i * new_image->y + j].green = sample[1];
            new_image->data[i * new_image->y + j].blue = sample[2];
        }
    }

    return new_image;
}

ppm_image *allocate_rescale()
{

    // alloc memory for image
    ppm_image *new_image = (ppm_image *)malloc(sizeof(ppm_image));
    if (!new_image)
    {
        fprintf(stderr, "Unable to allocate memory\n");
        exit(1);
    }
    new_image->x = RESCALE_X;
    new_image->y = RESCALE_Y;

    new_image->data = (ppm_pixel *)malloc(new_image->x * new_image->y * sizeof(ppm_pixel));
    if (!new_image)
    {
        fprintf(stderr, "Unable to allocate memory\n");
        exit(1);
    }
    return new_image;
}

void *apeleaza(void *arg)
{

    struct image *im = (struct image *)arg;
    if (im->image != im->scaled_image)
    {
        im->scaled_image = rescale_image(im);
    }
    pthread_barrier_wait(im->barrier);
    im->grid = sample_grid(im->scaled_image, im->grid, im->thread_id, im->N);
    pthread_barrier_wait(im->barrier);
    march(im->scaled_image, im->grid, im->contour_map, im->thread_id, im->N);
    pthread_barrier_wait(im->barrier);

    pthread_exit(NULL);
}

unsigned char **allocate_grid(ppm_image *image)
{

    int p = image->x / STEP;
    int q = image->y / STEP;
    unsigned char **grid = (unsigned char **)malloc((p + 1) * sizeof(unsigned char *));
    if (!grid)
    {
        fprintf(stderr, "Unable to allocate memory\n");
        exit(1);
    }

    for (int i = 0; i <= p; i++)
    {
        grid[i] = (unsigned char *)malloc((q + 1) * sizeof(unsigned char));
        if (!grid[i])
        {
            fprintf(stderr, "Unable to allocate memory\n");
            exit(1);
        }
    }
    return grid;
}

int main(int argc, char *argv[])
{
    if (argc < 4)
    {
        fprintf(stderr, "Usage: ./tema1 <in_file> <out_file> <P>\n");
        return 1;
    }

    int N = atoi(argv[3]);

    struct image *imagine = (struct image *)malloc(N * sizeof(struct image));

    pthread_t *threads = (pthread_t *)malloc(N * sizeof(pthread_t));

    pthread_barrier_t barrier;

    pthread_barrier_init(&barrier, NULL, N);

    ppm_image *image = read_ppm(argv[1]);

    // 0. Initialize contour map
    ppm_image **contour_map = init_contour_map();
    ppm_image *scaled_image;

    // 1. Rescale the image
    if (image->x <= RESCALE_X && image->y <= RESCALE_Y)
    {
        // no need to rescale
        scaled_image = image;
    }
    else
    {
        scaled_image = allocate_rescale();
    }

    unsigned char **grid = allocate_grid(scaled_image);

    for (int i = 0; i < N; i++)
    {
        imagine[i].N = N;
        imagine[i].thread_id = i;
        imagine[i].image = image;
        imagine[i].scaled_image = scaled_image;
        imagine[i].grid = grid;
        imagine[i].contour_map = contour_map;
        imagine[i].barrier = &barrier;

        pthread_create(&threads[i], NULL, apeleaza, &imagine[i]);
    }

    for (int i = 0; i < N; i++)
    {
        pthread_join(threads[i], NULL);
    }

    // 4. Write output
    write_ppm(scaled_image, argv[2]);
    pthread_barrier_destroy(&barrier);

    return 0;
}
