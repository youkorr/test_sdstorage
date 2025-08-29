# Dans __init__.py - Configuration avec auto_load global

from __future__ import annotations

import logging
from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import display, image
from esphome import automation
from esphome.const import (
    CONF_FILE,
    CONF_ID,
    CONF_PLATFORM,
    CONF_RESIZE,
    CONF_TYPE,
)
from esphome.core import CORE

_LOGGER = logging.getLogger(__name__)

DOMAIN = "storage"
DEPENDENCIES = ["display"]

# Namespaces
storage_ns = cg.esphome_ns.namespace("storage")
sd_mmc_card_ns = cg.esphome_ns.namespace("sd_mmc_card")

# Classes
StorageComponent = storage_ns.class_("StorageComponent", cg.Component)
SdImageComponent = storage_ns.class_("SdImageComponent", cg.Component, image.Image_)
SdMmc = sd_mmc_card_ns.class_("SdMmc")

# Configuration keys
CONF_STORAGE_COMPONENT = "storage_component"
CONF_ROOT_PATH = "root_path"
CONF_OUTPUT_FORMAT = "format"
CONF_BYTE_ORDER = "byte_order"
CONF_SD_COMPONENT = "sd_component"
CONF_SD_IMAGES = "sd_images"
CONF_FILE_PATH = "file_path"
CONF_AUTO_LOAD = "auto_load"  # Maintenant au niveau global

# Image format mappings
CONF_OUTPUT_IMAGE_FORMATS = {
    "RGB565": "RGB565",
    "RGB888": "RGB888",
    "RGBA": "RGBA",
}

CONF_BYTE_ORDERS = {
    "LITTLE_ENDIAN": "LITTLE_ENDIAN",
    "BIG_ENDIAN": "BIG_ENDIAN",
}

# Actions
SdImageLoadAction = storage_ns.class_("SdImageLoadAction", automation.Action)
SdImageUnloadAction = storage_ns.class_("SdImageUnloadAction", automation.Action)

# Schema pour SdImageComponent - SUPPRESSION de auto_load individuel
SD_IMAGE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SdImageComponent),
        cv.Required(CONF_FILE_PATH): cv.string,
        cv.Optional(CONF_OUTPUT_FORMAT, default="RGB565"): cv.enum(CONF_OUTPUT_IMAGE_FORMATS, upper=True),
        cv.Optional(CONF_BYTE_ORDER, default="LITTLE_ENDIAN"): cv.enum(CONF_BYTE_ORDERS, upper=True),
        cv.Optional(CONF_RESIZE): cv.dimensions,
        cv.Optional(CONF_TYPE, default="SD_IMAGE"): cv.string,
        # SUPPRIMÉ: auto_load individuel - maintenant géré au niveau global
    }
)

# Schema principal pour StorageComponent AVEC auto_load global
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(StorageComponent),
        cv.Optional(CONF_PLATFORM, default="sd_direct"): cv.string,
        cv.Optional(CONF_SD_COMPONENT): cv.use_id(SdMmc),
        cv.Optional(CONF_ROOT_PATH, default="/"): cv.string,
        cv.Optional(CONF_AUTO_LOAD, default=True): cv.boolean,  # AUTO_LOAD GLOBAL
        cv.Optional(CONF_SD_IMAGES, default=[]): cv.ensure_list(SD_IMAGE_SCHEMA),
    }
).extend(cv.COMPONENT_SCHEMA)

# Action schemas (inchangés)
LOAD_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(SdImageComponent),
    cv.Optional(CONF_FILE_PATH): cv.templatable(cv.string),
})

UNLOAD_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(SdImageComponent),
})

async def sd_image_load_action_to_code(config, action_id, template_arg, args):
    """Action to load an image from SD"""
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    if CONF_FILE_PATH in config:
        template_ = await cg.templatable(config[CONF_FILE_PATH], args, cg.std_string)
        cg.add(var.set_file_path(template_))
    return var

async def sd_image_unload_action_to_code(config, action_id, template_arg, args):
    """Action to unload an image"""
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    return var

# Register actions
automation.register_action(
    "sd_image.load",
    SdImageLoadAction,
    LOAD_ACTION_SCHEMA
)(sd_image_load_action_to_code)

automation.register_action(
    "sd_image.unload",
    SdImageUnloadAction,
    UNLOAD_ACTION_SCHEMA
)(sd_image_unload_action_to_code)

async def to_code(config):
    """Generate C++ code for storage component avec auto_load global"""

    # Create main component
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Configure main component
    cg.add(var.set_platform(config[CONF_PLATFORM]))
    cg.add(var.set_root_path(config[CONF_ROOT_PATH]))

    # NOUVEAU: Configuration auto_load global
    cg.add(var.set_auto_load(config[CONF_AUTO_LOAD]))

    if CONF_SD_COMPONENT in config:
        sd_comp = await cg.get_variable(config[CONF_SD_COMPONENT])
        cg.add(var.set_sd_component(sd_comp))

    # Add libraries and defines
    cg.add_library("pngle", "1.1.0")
    cg.add_define("USE_PNGLE")
    cg.add_define("CONFIG_ESPHOME_ENABLE_PNGLE")
    
    # Defines pour le système hybride
    if config[CONF_AUTO_LOAD]:
        cg.add_define("USE_GLOBAL_AUTO_LOAD")
        _LOGGER.info("Storage configured with global auto-load enabled")
    else:
        cg.add_define("USE_ON_DEMAND_ONLY")
        _LOGGER.info("Storage configured for on-demand loading only")
    
    cg.add_define("USE_HYBRID_LOADING_SYSTEM")
    
    # Configure SD images
    if CONF_SD_IMAGES in config:
        _LOGGER.info(f"Configuring {len(config[CONF_SD_IMAGES])} SD images with auto_load={config[CONF_AUTO_LOAD]}")
        for img_config in config[CONF_SD_IMAGES]:
            await setup_sd_image_component(img_config, var)

async def setup_sd_image_component(config, parent_storage):
    """Configure an SdImageComponent avec système hybride global"""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Link to parent storage component (s'enregistre automatiquement)
    cg.add(var.set_storage_component(parent_storage))
    cg.add(var.set_file_path(config[CONF_FILE_PATH]))

    # Set format and byte order
    output_format_str = config[CONF_OUTPUT_FORMAT]
    byte_order_str = config[CONF_BYTE_ORDER]

    cg.add(var.set_output_format_string(output_format_str))
    cg.add(var.set_byte_order_string(byte_order_str))

    if CONF_RESIZE in config:
        cg.add(var.set_resize(config[CONF_RESIZE][0], config[CONF_RESIZE][1]))

    return var

