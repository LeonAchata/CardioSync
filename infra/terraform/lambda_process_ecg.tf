# ProcessECG: S3 raw/*.bin -> filtering, R-peak detection, plots -> processed bucket.
# Packaged as a container image because numpy/scipy/matplotlib exceed the zip limit.

locals {
  process_src = "${local.lambdas_dir}/process_ecg"

  # Rebuild the image only when the Lambda sources change
  process_src_hash = sha1(join("", [
    for f in sort(fileset(local.process_src, "**")) : filesha1("${local.process_src}/${f}")
    if !strcontains(f, "__pycache__")
  ]))
}

resource "aws_ecr_repository" "process_ecg" {
  name                 = "${local.name}-process-ecg"
  image_tag_mutability = "MUTABLE"
  force_delete         = var.force_destroy

  image_scanning_configuration {
    scan_on_push = true
  }
}

resource "aws_ecr_lifecycle_policy" "process_ecg" {
  repository = aws_ecr_repository.process_ecg.name
  policy = jsonencode({
    rules = [{
      rulePriority = 1
      description  = "Keep the last 5 images"
      selection = {
        tagStatus   = "any"
        countType   = "imageCountMoreThan"
        countNumber = 5
      }
      action = { type = "expire" }
    }]
  })
}

resource "docker_image" "process_ecg" {
  name = "${aws_ecr_repository.process_ecg.repository_url}:${substr(local.process_src_hash, 0, 12)}"

  build {
    context  = local.process_src
    platform = "linux/amd64"
  }

  triggers = {
    src_hash = local.process_src_hash
  }
}

resource "docker_registry_image" "process_ecg" {
  name          = docker_image.process_ecg.name
  keep_remotely = true

  triggers = {
    src_hash = local.process_src_hash
  }
}

resource "aws_iam_role" "process_ecg" {
  name               = "${local.name}-process-ecg"
  assume_role_policy = data.aws_iam_policy_document.lambda_assume.json
}

resource "aws_iam_role_policy_attachment" "process_ecg_logs" {
  role       = aws_iam_role.process_ecg.name
  policy_arn = "arn:aws:iam::aws:policy/service-role/AWSLambdaBasicExecutionRole"
}

data "aws_iam_policy_document" "process_ecg" {
  statement {
    actions   = ["s3:GetObject"]
    resources = ["${aws_s3_bucket.raw.arn}/raw/*"]
  }

  statement {
    actions   = ["s3:PutObject"]
    resources = ["${aws_s3_bucket.processed.arn}/processed/*"]
  }

  statement {
    actions   = ["sqs:SendMessage"]
    resources = [aws_sqs_queue.process_ecg_failures.arn]
  }
}

resource "aws_iam_role_policy" "process_ecg" {
  name   = "read-raw-write-processed"
  role   = aws_iam_role.process_ecg.id
  policy = data.aws_iam_policy_document.process_ecg.json
}

resource "aws_cloudwatch_log_group" "process_ecg" {
  name              = "/aws/lambda/${local.name}-process-ecg"
  retention_in_days = var.log_retention_days
}

resource "aws_lambda_function" "process_ecg" {
  function_name = "${local.name}-process-ecg"
  description   = "Filters CardioSync ECG sessions, detects heart rate and renders reports"
  role          = aws_iam_role.process_ecg.arn
  package_type  = "Image"
  image_uri     = "${aws_ecr_repository.process_ecg.repository_url}@${docker_registry_image.process_ecg.sha256_digest}"
  architectures = ["x86_64"]
  memory_size   = var.process_memory_mb
  timeout       = var.process_timeout_seconds

  environment {
    variables = {
      OUTPUT_BUCKET = aws_s3_bucket.processed.bucket
    }
  }

  depends_on = [
    aws_cloudwatch_log_group.process_ecg,
    aws_iam_role_policy_attachment.process_ecg_logs,
  ]
}

resource "aws_lambda_permission" "s3_invoke_process_ecg" {
  statement_id   = "AllowS3Invoke"
  action         = "lambda:InvokeFunction"
  function_name  = aws_lambda_function.process_ecg.function_name
  principal      = "s3.amazonaws.com"
  source_arn     = aws_s3_bucket.raw.arn
  source_account = local.account_id
}

# Sessions that still fail after the async retries land here for inspection
resource "aws_sqs_queue" "process_ecg_failures" {
  name                      = "${local.name}-process-ecg-failures"
  message_retention_seconds = 1209600 # 14 days
  sqs_managed_sse_enabled   = true
}

resource "aws_lambda_function_event_invoke_config" "process_ecg" {
  function_name          = aws_lambda_function.process_ecg.function_name
  maximum_retry_attempts = 2

  destination_config {
    on_failure {
      destination = aws_sqs_queue.process_ecg_failures.arn
    }
  }
}
