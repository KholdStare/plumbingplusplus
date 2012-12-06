## Description

This library aims to simplify pipelining transformations, making them
concurrent, and automatically running each transformation on a separate thread

### Synopsis (API Proposal)

As an example, say you have a nice set of composable functions, and you want to
feed data through them:

    Image<RGB> loadImage(std::string path);
    Image<RGB>& processImage(Image<RGB>& image);
    void saveImage(Image<RGB>const& image);

To process several images, the simple imperative approach is a for loop:

    std::vector<std:string> paths{"tree.jpg", "mountain.jpg", "car.jpg"};

    for (auto&& path : paths)
    {
        Image<RGB> image = loadImage(path);
        saveImage(processImage(image));
    }

It gets the job done but is single threaded and not concurrent. Most of the time
the program will be blocked, waiting for IO to complete. Since it is single
threaded, parallelism cannot be exploited. Wouldn't it be nice if one image was
being read, while another was being processed, while another was being saved?

Enter Plumbing++, which automatically creates threads for each processing step,
and joins each with a concurrent FIFO:

    
    std::vector<std:string> paths{"tree.jpg", "mountain.jpg", "car.jpg"};

    Plumbing::Source(paths).connect(loadImage)
                           .connect(processImage)
                           .connect(saveImage)

Done! Each connect call returns the end of a concurrent FIFO called a Sink:

    Plumbing::Sink<Image<RGB>> imageSink = Plumbing::Source(paths).connect(loadImage)

These can be freely iterated over, or used in stl algorithms:

    std::vector<Image<RGB>> imageCopies;
    std::copy(imageSink.begin(), imageSink.end(), std::back_inserter(imageCopies))

    for (auto&& elem : anotherSink)
    {
        std::cout << elem << std::endl;
    }

Alternate ways of connecting the functions could be implemented:

    // "Unix" way
    Plumbing::Source(paths) | loadImage | processImage | saveImage;

    // "Almost like something similar to Haskell" way
    Plumbing::Source(paths) >> loadImage >> processImage >> saveImage;

### Progress

This project is in its very early stages, so only a simple (not yet fully
functional) concurrent FIFO is implemented. The intent is to make it gel well
with move semantics and C++11 for efficient passing of objects, as well as
properly closing/migrating pipes accross threads (migrating could be difficult).
