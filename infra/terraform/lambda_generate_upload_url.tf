# GenerateUploadURL: IoT rule -> presigned S3 PUT URL -> MQTT reply to the device

data "archive_file" "generate_upload_url" {
  type        = "zip"
  source_file = "${local.lambdas_dir}/generate_upload_url/app.py"
  output_path = "${path.module}/.build/generate_upload_url.zip"
}

data "aws_iam_policy_document" "lambda_assume" {
  statement {
    actions = ["sts:AssumeRole"]
    principals {
      type        = "Service"
      identifiers = ["lambda.amazonaws.com"]
    }
  }
}

resource "aws_iam_role" "generate_upload_url" {
  name               = "${local.name}-generate-upload-url"
  assume_role_policy = data.aws_iam_policy_document.lambda_assume.json
}

resource "aws_iam_role_policy_attachment" "generate_upload_url_logs" {
  role       = aws_iam_role.generate_upload_url.name
  policy_arn = "arn:aws:iam::aws:policy/service-role/AWSLambdaBasicExecutionRole"
}

data "aws_iam_policy_document" "generate_upload_url" {
  # The presigned URL inherits these permissions: PUT only, raw/ prefix only
  statement {
    actions   = ["s3:PutObject"]
    resources = ["${aws_s3_bucket.raw.arn}/raw/*"]
  }

  statement {
    actions   = ["iot:Publish"]
    resources = ["${local.iot_arn_prefix}:topic/${local.topic_response_prefix}/*"]
  }
}

resource "aws_iam_role_policy" "generate_upload_url" {
  name   = "presign-and-reply"
  role   = aws_iam_role.generate_upload_url.id
  policy = data.aws_iam_policy_document.generate_upload_url.json
}

resource "aws_cloudwatch_log_group" "generate_upload_url" {
  name              = "/aws/lambda/${local.name}-generate-upload-url"
  retention_in_days = var.log_retention_days
}

resource "aws_lambda_function" "generate_upload_url" {
  function_name    = "${local.name}-generate-upload-url"
  description      = "Issues S3 presigned upload URLs to CardioSync devices over MQTT"
  role             = aws_iam_role.generate_upload_url.arn
  runtime          = "python3.12"
  architectures    = ["arm64"]
  handler          = "app.lambda_handler"
  filename         = data.archive_file.generate_upload_url.output_path
  source_code_hash = data.archive_file.generate_upload_url.output_base64sha256
  memory_size      = 128
  timeout          = 10

  environment {
    variables = {
      RAW_BUCKET            = aws_s3_bucket.raw.bucket
      RESPONSE_TOPIC_PREFIX = local.topic_response_prefix
      IOT_DATA_ENDPOINT     = data.aws_iot_endpoint.ats.endpoint_address
      URL_EXPIRES_SECONDS   = tostring(var.url_expires_seconds)
      MAX_FILE_BYTES        = tostring(var.max_file_bytes)
    }
  }

  depends_on = [
    aws_cloudwatch_log_group.generate_upload_url,
    aws_iam_role_policy_attachment.generate_upload_url_logs,
  ]
}
