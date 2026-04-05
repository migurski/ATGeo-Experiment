FROM public.ecr.aws/lambda/provided:al2023 AS builder

# Build tools
RUN dnf install -y \
    cmake \
    gcc \
    gcc-c++ \
    make \
    git \
    tar \
    gzip \
    zip \
    wget \
    openssl-devel \
    libcurl-devel \
    zlib-devel \
    sqlite-devel \
    pcre2-devel \
    pkgconf \
    && dnf clean all

WORKDIR /build

# Build PROJ (required by GDAL, no optional drivers needed)
ARG PROJ_VERSION=9.5.1
RUN wget -q "https://github.com/OSGeo/PROJ/releases/download/${PROJ_VERSION}/proj-${PROJ_VERSION}.tar.gz" \
    && tar xf proj-${PROJ_VERSION}.tar.gz \
    && mkdir proj-build \
    && cd proj-build \
    && cmake /build/proj-${PROJ_VERSION} \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_TESTING=OFF \
        -DENABLE_TIFF=OFF \
        -DENABLE_CURL=OFF \
        -DBUILD_APPS=OFF \
    && make -j$(nproc) \
    && make install \
    && cd /build \
    && rm -rf proj-${PROJ_VERSION} proj-${PROJ_VERSION}.tar.gz proj-build

# Build minimal GDAL (GTiff + /vsis3/ only, static)
ARG GDAL_VERSION=3.10.2
RUN wget -q "https://github.com/OSGeo/gdal/releases/download/v${GDAL_VERSION}/gdal-${GDAL_VERSION}.tar.gz" \
    && tar xf gdal-${GDAL_VERSION}.tar.gz \
    && mkdir gdal-build \
    && cd gdal-build \
    && cmake /build/gdal-${GDAL_VERSION} \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DBUILD_SHARED_LIBS=OFF \
        -DGDAL_BUILD_OPTIONAL_DRIVERS=OFF \
        -DGDAL_ENABLE_DRIVER_GTIFF=ON \
        -DGDAL_USE_TIFF_INTERNAL=ON \
        -DGDAL_USE_GEOTIFF_INTERNAL=ON \
        -DGDAL_USE_CURL=ON \
        -DBUILD_PYTHON_BINDINGS=OFF \
        -DBUILD_APPS=OFF \
        -DENABLE_TESTING=OFF \
    && make -j$(nproc) \
    && make install \
    && cd /build \
    && rm -rf gdal-${GDAL_VERSION} gdal-${GDAL_VERSION}.tar.gz gdal-build

# Build AWS Lambda C++ runtime
RUN git clone --depth 1 https://github.com/awslabs/aws-lambda-cpp.git \
    && cd aws-lambda-cpp \
    && mkdir build \
    && cd build \
    && cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
    && make -j$(nproc) \
    && make install \
    && cd /build \
    && rm -rf aws-lambda-cpp

# Build AWS SDK for C++ (core only)
RUN git clone --recurse-submodules --depth 1 --branch 1.11.442 \
        https://github.com/aws/aws-sdk-cpp.git \
    && cd aws-sdk-cpp \
    && mkdir build \
    && cd build \
    && cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DBUILD_ONLY="core" \
        -DENABLE_TESTING=OFF \
        -DBUILD_SHARED_LIBS=OFF \
        -DCPP_STANDARD=17 \
    && make -j$(nproc) \
    && make install \
    && cd /build \
    && rm -rf aws-sdk-cpp

# Build the Lambda function (only this layer changes on source edits)
COPY lambda.cpp CMakeLists.txt /build/app/
RUN mkdir /build/app-build \
    && cd /build/app-build \
    && cmake /build/app \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH=/usr/local \
    && make -j$(nproc) \
    && make aws-lambda-package-atgeo-cpp

RUN mv /build/app-build/bootstrap.zip /lambda-cpp.zip

# Final stage — export only the zip
FROM scratch AS zip
COPY --from=builder /lambda-cpp.zip /lambda-cpp.zip

# CLI stage — minimal image with just the binary
FROM public.ecr.aws/amazonlinux/amazonlinux:2023-minimal AS cli
COPY --from=builder /build/app-build/bootstrap /usr/local/bin/atgeo
ENTRYPOINT ["/usr/local/bin/atgeo"]
