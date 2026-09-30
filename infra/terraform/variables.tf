variable "aws_region" {
  description = "AWS region for every resource."
  type        = string
  default     = "us-east-1"
}

variable "project_name" {
  description = "Prefix used to name resources and MQTT topics."
  type        = string
  default     = "cardiosync"

  validation {
    condition     = can(regex("^[a-z0-9-]{3,20}$", var.project_name))
    error_message = "Use 3-20 lowercase letters, digits or dashes."
  }
}

variable "environment" {
  description = "Environment name, added to resource names and tags."
  type        = string
  default     = "dev"
}

variable "device_ids" {
  description = "IoT thing names, one per ESP32. Each one becomes the device MQTT client ID."
  type        = list(string)
  default     = ["cardiosync-esp32-001"]

  validation {
    condition     = length(var.device_ids) > 0 && alltrue([for d in var.device_ids : can(regex("^[A-Za-z0-9_-]{1,64}$", d))])
    error_message = "Provide at least one device ID made of letters, digits, '_' or '-' (max 64 chars)."
  }
}

variable "firmware_device_id" {
  description = "Device whose aws_config.h is also written to firmware/include/. Defaults to the first device."
  type        = string
  default     = null
}

variable "write_firmware_config" {
  description = "Write firmware/include/aws_config.h so the firmware builds right after apply."
  type        = bool
  default     = true
}

variable "url_expires_seconds" {
  description = "Validity of the S3 presigned upload URLs."
  type        = number
  default     = 300
}

variable "max_file_bytes" {
  description = "Largest session file (bytes) a device may upload."
  type        = number
  default     = 10485760
}

variable "raw_data_expiration_days" {
  description = "Days before raw .bin files expire from S3 (0 = keep forever)."
  type        = number
  default     = 0
}

variable "process_memory_mb" {
  description = "Memory for the ProcessECG Lambda (also scales its CPU)."
  type        = number
  default     = 1024
}

variable "process_timeout_seconds" {
  description = "Timeout for the ProcessECG Lambda."
  type        = number
  default     = 120
}

variable "log_retention_days" {
  description = "CloudWatch Logs retention."
  type        = number
  default     = 14
}

variable "force_destroy" {
  description = "Allow `terraform destroy` to delete non-empty buckets and the ECR repository."
  type        = bool
  default     = false
}

variable "docker_host" {
  description = "Docker daemon address. Windows Docker Desktop: npipe:////./pipe/docker_engine. null = provider default / DOCKER_HOST."
  type        = string
  default     = null
}
