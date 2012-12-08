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

    (paths >> loadImage >> processImage >> saveImage).wait();

Done! Each connection takes an iterable object, and returns the end of a
concurrent FIFO called a <pre>Sink</pre>:

    Plumbing::Sink<Image<RGB>> imageSink = (paths >> loadImage)

These can be freely iterated over, or used in stl algorithms:

    std::vector<Image<RGB>> imageCopies;
    std::copy(imageSink.begin(), imageSink.end(), std::back_inserter(imageCopies))

    for (auto&& elem : anotherSink)
    {
        std::cout << elem << std::endl;
    }

Each <pre>Sink</pre> (being iterable) thus becomes the input to the next
connection. If the last function in the pipeline returns void, the connect call
returns a future, so the caller can wait for the whole computation to complete:

    std::future<void> future = (paths >> loadImage >> processImage >> saveImage);
    future.wait();

### Progress

The above features are currently implemented, but I am seeking constructive
criticism on the API and implementation.

Currently, several improvements can be made to the implementation regarding move
semantics, to save expensive copies.  It should be possible to move objects all
the way through the pipeline.

A concern with the API is the use of operator overloading. The <pre>operator
>></pre> template function may accept too broad a range of inputs, and also
leaks into the global namespace. The operator delegates to a regular function,
but composing with this function is awkward:

    Plumbing::connect(
        Plumbing::connect(
            Plumbing::connect(paths, loadImage),
            processImage
        ),
        saveImage
    );

An alternative can be a variadic template that takes a sequence of functions:

    Plumbing::connect(paths, loadImage, processImage, saveImage);

This seems like a good approach.
