FROM --platform=linux/arm64 ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt update -y \
 && apt install -y python3 python3-pip python3-gdal \
 && apt clean -y \
 && rm -rf /var/lib/apt/lists/*

ENV PIP_BREAK_SYSTEM_PACKAGES=1
RUN pip3 install 'awslambdaric==2.2.1'

COPY lambda.py geohash.py /var/task/

WORKDIR /var/task
ENTRYPOINT ["python3", "-m", "awslambdaric"]
CMD ["lambda.handler"]
