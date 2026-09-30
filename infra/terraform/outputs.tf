output "iot_endpoint" {
  description = "AWS IoT Core ATS data endpoint (AWS_IOT_ENDPOINT)."
  value       = data.aws_iot_endpoint.ats.endpoint_address
}

output "device_ids" {
  description = "Provisioned IoT things."
  value       = [for t in aws_iot_thing.device : t.name]
}

output "mqtt_topics" {
  description = "Topics used by the devices."
  value = {
    request  = local.topic_request
    response = "${local.topic_response_prefix}/<device_id>"
  }
}

output "raw_bucket" {
  description = "Bucket receiving the raw .bin session files."
  value       = aws_s3_bucket.raw.bucket
}

output "processed_bucket" {
  description = "Bucket holding metadata.json, signals.csv and plots per session."
  value       = aws_s3_bucket.processed.bucket
}

output "lambda_functions" {
  description = "Deployed Lambda function names."
  value = {
    generate_upload_url = aws_lambda_function.generate_upload_url.function_name
    process_ecg         = aws_lambda_function.process_ecg.function_name
  }
}

output "process_failures_queue_url" {
  description = "SQS queue with sessions that could not be processed."
  value       = aws_sqs_queue.process_ecg_failures.url
}

output "firmware_config_files" {
  description = "Generated aws_config.h per device."
  value       = { for id, f in local_sensitive_file.device_config : id => abspath(f.filename) }
}
