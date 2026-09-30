# Raw session files uploaded by the devices: raw/<device_id>/<session_id>.bin
resource "aws_s3_bucket" "raw" {
  bucket        = "${local.name}-raw-${local.suffix}"
  force_destroy = var.force_destroy
}

# Processing results: processed/<device_id>/<session_id>/{metadata.json,signals.csv,*.png}
resource "aws_s3_bucket" "processed" {
  bucket        = "${local.name}-processed-${local.suffix}"
  force_destroy = var.force_destroy
}

locals {
  buckets = {
    raw       = aws_s3_bucket.raw
    processed = aws_s3_bucket.processed
  }
}

resource "aws_s3_bucket_public_access_block" "this" {
  for_each = local.buckets

  bucket                  = each.value.id
  block_public_acls       = true
  block_public_policy     = true
  ignore_public_acls      = true
  restrict_public_buckets = true
}

resource "aws_s3_bucket_server_side_encryption_configuration" "this" {
  for_each = local.buckets

  bucket = each.value.id
  rule {
    apply_server_side_encryption_by_default {
      sse_algorithm = "AES256"
    }
  }
}

data "aws_iam_policy_document" "tls_only" {
  for_each = local.buckets

  statement {
    sid       = "DenyInsecureTransport"
    effect    = "Deny"
    actions   = ["s3:*"]
    resources = [each.value.arn, "${each.value.arn}/*"]

    principals {
      type        = "*"
      identifiers = ["*"]
    }

    condition {
      test     = "Bool"
      variable = "aws:SecureTransport"
      values   = ["false"]
    }
  }
}

resource "aws_s3_bucket_policy" "tls_only" {
  for_each = local.buckets

  bucket = each.value.id
  policy = data.aws_iam_policy_document.tls_only[each.key].json

  depends_on = [aws_s3_bucket_public_access_block.this]
}

resource "aws_s3_bucket_lifecycle_configuration" "raw" {
  count = var.raw_data_expiration_days > 0 ? 1 : 0

  bucket = aws_s3_bucket.raw.id
  rule {
    id     = "expire-raw-sessions"
    status = "Enabled"
    filter {
      prefix = "raw/"
    }
    expiration {
      days = var.raw_data_expiration_days
    }
  }
}

resource "aws_s3_bucket_notification" "raw" {
  bucket = aws_s3_bucket.raw.id

  lambda_function {
    lambda_function_arn = aws_lambda_function.process_ecg.arn
    events              = ["s3:ObjectCreated:*"]
    filter_prefix       = "raw/"
    filter_suffix       = ".bin"
  }

  depends_on = [aws_lambda_permission.s3_invoke_process_ecg]
}
