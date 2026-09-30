# Container image for the ProcessECG Lambda
# (numpy + scipy + matplotlib exceed the 250 MB zip package limit)
FROM public.ecr.aws/lambda/python:3.12

# matplotlib needs a writable config/cache dir; only /tmp is writable in Lambda
ENV MPLCONFIGDIR=/tmp/matplotlib

COPY requirements.txt ${LAMBDA_TASK_ROOT}/
RUN pip install --no-cache-dir -r ${LAMBDA_TASK_ROOT}/requirements.txt

COPY app.py ${LAMBDA_TASK_ROOT}/
COPY cardiosync ${LAMBDA_TASK_ROOT}/cardiosync

CMD ["app.lambda_handler"]
