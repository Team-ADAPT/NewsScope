FROM ubuntu:22.04 AS builder
RUN apt-get update && apt-get install -y clang make && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY . .
RUN make all

FROM ubuntu:22.04
RUN apt-get update && apt-get install -y libstdc++6 && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=builder /app/build/newsscope_webserver ./
COPY --from=builder /app/data ./data
COPY --from=builder /app/web ./web
EXPOSE 8080
CMD ["./newsscope_webserver"]
