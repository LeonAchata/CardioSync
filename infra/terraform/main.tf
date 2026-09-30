locals {
  name   = "${var.project_name}-${var.environment}"
  suffix = random_id.suffix.hex

  account_id = data.aws_caller_identity.current.account_id

  # MQTT topics shared by the IoT policy, the IoT rule, the Lambda and the firmware
  topic_request         = "${var.project_name}/upload-request"
  topic_response_prefix = "${var.project_name}/upload-url"

  firmware_device_id = coalesce(var.firmware_device_id, var.device_ids[0])

  lambdas_dir = "${path.module}/../../cloud/lambdas"
}

resource "random_id" "suffix" {
  byte_length = 3
}
