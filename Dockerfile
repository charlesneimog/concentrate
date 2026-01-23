FROM python:3.12-slim

WORKDIR /app

COPY niriserver.py index.html /app/

EXPOSE 8079

ENV NIRI_HOST=0.0.0.0
ENV NIRI_PORT=8079

CMD ["python", "-u", "niriserver.py"]
