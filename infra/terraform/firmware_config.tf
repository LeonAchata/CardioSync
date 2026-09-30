# Renders aws_config.h (endpoint, topics, certificates) for every device so the
# firmware can be flashed right after `terraform apply`.

data "http" "amazon_root_ca" {
  url = "https://www.amazontrust.com/repository/AmazonRootCA1.pem"
}

locals {
  firmware_configs = {
    for id in var.device_ids : id => templatefile("${path.module}/templates/aws_config.h.tftpl", {
      endpoint       = data.aws_iot_endpoint.ats.endpoint_address
      device_id      = id
      topic_request  = local.topic_request
      topic_response = "${local.topic_response_prefix}/${id}"
      root_ca        = trimspace(data.http.amazon_root_ca.response_body)
      certificate    = trimspace(aws_iot_certificate.device[id].certificate_pem)
      private_key    = trimspace(aws_iot_certificate.device[id].private_key)
    })
  }
}

resource "local_sensitive_file" "device_config" {
  for_each = local.firmware_configs

  filename        = "${path.module}/generated/${each.key}/aws_config.h"
  content         = each.value
  file_permission = "0600"
}

resource "local_sensitive_file" "firmware_config" {
  count = var.write_firmware_config ? 1 : 0

  filename        = "${path.module}/../../firmware/include/aws_config.h"
  content         = local.firmware_configs[local.firmware_device_id]
  file_permission = "0600"
}
