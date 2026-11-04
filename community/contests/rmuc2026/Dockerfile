FROM python:3.12-slim@sha256:a6e34c598f2467ed0e9a8d349809fcd8b5c603269512df273a0bb1784edc11b1
ENV PYTHONDONTWRITEBYTECODE=1 PYTHONHASHSEED=0 PYTHONUNBUFFERED=1
COPY rmuc_arena/sdk.py rmuc_arena/worker.py /runtime/
COPY config/rules.json /runtime/rules.json
USER 65534:65534
WORKDIR /submission
ENTRYPOINT ["python", "-I", "/runtime/worker.py"]
