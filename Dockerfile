FROM gcc:14-bookworm
WORKDIR /app
COPY include/ include/
COPY src/ src/
RUN gcc -std=c11 -O2 -Wall -Wextra -Iinclude src/*.c -o app -pthread
CMD ["./app"]
