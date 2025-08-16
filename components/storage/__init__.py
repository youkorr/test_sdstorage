from __future__ import annotations

import logging
from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import display, image
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
CONF_OUTPUT_FORMAT = "output_format"
CONF_BYTE_ORDER = "byte_order"
CONF_SD_COMPONENT = "sd_component"

# Enums
OutputImageFormat = storage_ns.enum("OutputImageFormat")
OUTPUT_IMAGE_FORMATS = {
    "RGB565": OutputImageFormat.rgb565,
    "RGB888": OutputImageFormat.rgb888,
    "RGBA": OutputImageFormat.rgba,
}

ByteOrder = storage_ns.enum("ByteOrder")
BYTE_ORDERS = {
    "LITTLE_ENDIAN": ByteOrder.little_endian,
    "BIG_ENDIAN": ByteOrder.big_endian,
}

# Encodeur personnalisé pour les images SD
class SdImageEncoder(image.ImageEncoder):
    """Encodeur d'image pour les images stockées sur carte SD"""
    
    allow_config = {image.CONF_ALPHA_CHANNEL, image.CONF_CHROMA_KEY, image.CONF_OPAQUE}

    def __init__(self, width, height, transparency, dither, invert_alpha, storage_component, file_path, output_format="RGB565"):
        super().__init__(width, height, transparency, dither, invert_alpha)
        self.storage_component = storage_component
        self.file_path = file_path
        self.output_format = output_format
        
    @staticmethod
    def validate(value):
        # Validation spécifique pour SD image
        if not value.get(CONF_FILE):
            raise cv.Invalid("File path is required for SD images")
        return value

    def convert(self, image_data, path):
        # Pour les images SD, on ne fait pas de conversion ici
        # La conversion se fait côté C++ lors du chargement depuis la SD
        return image_data

    def encode(self, pixel):
        # L'encodage se fait côté C++
        pass

# Ajouter le type SD_IMAGE au système d'images d'ESPHome
def register_sd_image_type():
    """Enregistre le type d'image SD dans le système ESPHome"""
    # Ajouter à la liste des types d'images disponibles
    image.IMAGE_TYPE["SD_IMAGE"] = SdImageEncoder

# Schema de validation pour StorageComponent
STORAGE_COMPONENT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(StorageComponent),
        cv.Optional(CONF_PLATFORM, default="esp32"): cv.string,
        cv.Optional(CONF_ROOT_PATH, default="/"): cv.string,
        cv.Optional(CONF_SD_COMPONENT): cv.use_id(SdMmc),
    }
).extend(cv.COMPONENT_SCHEMA)

# Schema pour SdImageComponent - Compatible avec le système image d'ESPHome
SD_IMAGE_SCHEMA = cv.All(
    {
        cv.GenerateID(): cv.declare_id(SdImageComponent),
        cv.Required(CONF_FILE): cv.string,  # Chemin sur la carte SD
        cv.Optional(CONF_STORAGE_COMPONENT): cv.use_id(StorageComponent),
        cv.Optional(CONF_OUTPUT_FORMAT, default="RGB565"): cv.enum(OUTPUT_IMAGE_FORMATS, upper=True),
        cv.Optional(CONF_BYTE_ORDER, default="LITTLE_ENDIAN"): cv.enum(BYTE_ORDERS, upper=True),
        cv.Optional(CONF_RESIZE): cv.dimensions,
        # Ajouter les options standard des images
        **{k: v for k, v in image.OPTIONS_SCHEMA.items() if k != image.CONF_TRANSPARENCY},
        cv.Optional(image.CONF_TRANSPARENCY, default=image.CONF_OPAQUE): image.validate_transparency(),
    },
    cv.has_at_least_one_key(CONF_FILE),
)

# Schema de configuration principal
CONFIG_SCHEMA = cv.Any(
    # Configuration simple : liste de StorageComponents
    cv.ensure_list(STORAGE_COMPONENT_SCHEMA),
    # Configuration avancée : dictionnaire avec storage et images
    cv.Schema({
        cv.Optional("storage"): cv.ensure_list(STORAGE_COMPONENT_SCHEMA),
        cv.Optional("sd_images"): cv.ensure_list(SD_IMAGE_SCHEMA),
    })
)

# Actions
SdImageLoadAction = storage_ns.class_("SdImageLoadAction", cg.Action)
SdImageUnloadAction = storage_ns.class_("SdImageUnloadAction", cg.Action)

@cv.templatable
def sd_image_load_action(var, config, args):
    """Action pour charger une image depuis la SD"""
    action = cg.new_Pvariable(config[CONF_ID], var)
    if CONF_FILE in config:
        template_ = cg.templatable(config[CONF_FILE], args, cg.std_string)
        cg.add(action.set_file_path(template_))
    return action

@cv.templatable  
def sd_image_unload_action(var, config, args):
    """Action pour décharger une image"""
    action = cg.new_Pvariable(config[CONF_ID], var)
    return action

async def to_code(config):
    """Génère le code C++ pour les composants storage"""
    
    # Enregistrer le type d'image SD
    register_sd_image_type()
    
    if isinstance(config, list):
        # Configuration simple : liste de StorageComponents
        for conf in config:
            await setup_storage_component(conf)
    else:
        # Configuration avancée
        if "storage" in config:
            for conf in config["storage"]:
                await setup_storage_component(conf)
        
        if "sd_images" in config:
            for conf in config["sd_images"]:
                await setup_sd_image_component(conf)

async def setup_storage_component(config):
    """Configure un StorageComponent"""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    cg.add(var.set_platform(config[CONF_PLATFORM]))
    cg.add(var.set_root_path(config[CONF_ROOT_PATH]))
    
    if CONF_SD_COMPONENT in config:
        sd_comp = await cg.get_variable(config[CONF_SD_COMPONENT])
        cg.add(var.set_sd_component(sd_comp))

async def setup_sd_image_component(config):
    """Configure un SdImageComponent"""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    cg.add(var.set_file_path(config[CONF_FILE]))
    
    if CONF_STORAGE_COMPONENT in config:
        storage = await cg.get_variable(config[CONF_STORAGE_COMPONENT])
        cg.add(var.set_storage_component(storage))
    
    cg.add(var.set_output_format_string(config[CONF_OUTPUT_FORMAT]))
    cg.add(var.set_byte_order_string(config[CONF_BYTE_ORDER]))
    
    if CONF_RESIZE in config:
        cg.add(var.set_resize(config[CONF_RESIZE][0], config[CONF_RESIZE][1]))

# Intégration avec le système d'images ESPHome
def validate_sd_image(value):
    """Validation pour les images SD dans la configuration image"""
    if value.get(CONF_TYPE) == "SD_IMAGE":
        # Validation spécifique aux images SD
        if not value.get(CONF_FILE):
            raise cv.Invalid("file is required for SD_IMAGE type")
        if not value.get(CONF_STORAGE_COMPONENT):
            raise cv.Invalid("storage_component is required for SD_IMAGE type")
    return value

# Étendre le schema des images ESPHome pour supporter SD_IMAGE
def extend_image_schema():
    """Étend le schéma d'images ESPHome pour supporter les images SD"""
    # Ajouter SD_IMAGE aux types d'images disponibles
    extended_schema = image.IMAGE_SCHEMA.extend({
        cv.Optional(CONF_STORAGE_COMPONENT): cv.use_id(StorageComponent),
        cv.Optional(CONF_OUTPUT_FORMAT, default="RGB565"): cv.enum(OUTPUT_IMAGE_FORMATS, upper=True),
        cv.Optional(CONF_BYTE_ORDER, default="LITTLE_ENDIAN"): cv.enum(BYTE_ORDERS, upper=True),
    })
    
    return cv.All(extended_schema, validate_sd_image)

# Hook pour modifier le comportement des images ESPHome
async def image_to_code_hook(config):
    """Hook appelé lors de la génération du code pour les images"""
    if config.get(CONF_TYPE) == "SD_IMAGE":
        # Générer un SdImageComponent au lieu d'une Image standard
        var = cg.new_Pvariable(config[CONF_ID])
        await cg.register_component(var, config)
        
        cg.add(var.set_file_path(config[CONF_FILE]))
        
        if CONF_STORAGE_COMPONENT in config:
            storage = await cg.get_variable(config[CONF_STORAGE_COMPONENT])
            cg.add(var.set_storage_component(storage))
        
        cg.add(var.set_output_format_string(config[CONF_OUTPUT_FORMAT]))
        cg.add(var.set_byte_order_string(config[CONF_BYTE_ORDER]))
        
        return var
    
    return None

# Enregistrer l'extension
def setup_hooks():
    """Configure les hooks d'intégration avec ESPHome"""
    # Ajouter le hook pour les images
    if hasattr(image, 'register_image_hook'):
        image.register_image_hook(image_to_code_hook)
    
    # Enregistrer le type d'image
    register_sd_image_type()

# Appeler setup lors de l'import
setup_hooks()
