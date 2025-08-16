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

# Actions - Using standard ESPHome automation framework
SdImageLoadAction = storage_ns.class_("SdImageLoadAction", automation.Action)
SdImageUnloadAction = storage_ns.class_("SdImageUnloadAction", automation.Action)

# Schema pour SdImageComponent - Compatible avec le système image ESPHome
SD_IMAGE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SdImageComponent),
        cv.Required(CONF_FILE_PATH): cv.string,  # file_path au lieu de file
        cv.Optional(CONF_OUTPUT_FORMAT, default="rgb565"): cv.enum(OUTPUT_IMAGE_FORMATS, upper=True),
        cv.Optional(CONF_BYTE_ORDER, default="little_endian"): cv.enum(BYTE_ORDERS, upper=True),
        cv.Optional(CONF_RESIZE): cv.dimensions,
        # Ajouter le type pour la compatibilité avec LVGL et autres composants
        cv.Optional(CONF_TYPE, default="SD_IMAGE"): cv.string,
        # Ajouter les options standard des images pour la compatibilité (simplifiées)
        cv.Optional(image.CONF_TRANSPARENCY, default=image.CONF_OPAQUE): image.validate_transparency(),
    }
)

# Schema de validation pour StorageComponent - Structure selon votre format désiré
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(StorageComponent),
        cv.Optional(CONF_PLATFORM, default="sd_direct"): cv.string,
        cv.Optional(CONF_SD_COMPONENT): cv.use_id(SdMmc),
        cv.Optional(CONF_ROOT_PATH, default="/"): cv.string,
        cv.Optional(CONF_SD_IMAGES, default=[]): cv.ensure_list(SD_IMAGE_SCHEMA),
    }
).extend(cv.COMPONENT_SCHEMA)

# Actions pour charger/décharger des images - Using automation.register_action
LOAD_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(SdImageComponent),
    cv.Optional(CONF_FILE_PATH): cv.templatable(cv.string),
})

UNLOAD_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(SdImageComponent),
})

async def sd_image_load_action_to_code(config, action_id, template_arg, args):
    """Action pour charger une image depuis la SD"""
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    if CONF_FILE_PATH in config:
        template_ = await cg.templatable(config[CONF_FILE_PATH], args, cg.std_string)
        cg.add(var.set_file_path(template_))
    return var

async def sd_image_unload_action_to_code(config, action_id, template_arg, args):
    """Action pour décharger une image"""
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    return var

# Enregistrer les actions
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
    """Génère le code C++ pour le composant storage"""
    
    # Créer le composant principal
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    # Configuration du composant principal
    cg.add(var.set_platform(config[CONF_PLATFORM]))
    cg.add(var.set_root_path(config[CONF_ROOT_PATH]))
    
    if CONF_SD_COMPONENT in config:
        sd_comp = await cg.get_variable(config[CONF_SD_COMPONENT])
        cg.add(var.set_sd_component(sd_comp))
    
    # Configuration des images SD
    if CONF_SD_IMAGES in config:
        for img_config in config[CONF_SD_IMAGES]:
            await setup_sd_image_component(img_config, var)

async def setup_sd_image_component(config, parent_storage):
    """Configure un SdImageComponent"""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    # Lier au composant storage parent
    cg.add(var.set_storage_component(parent_storage))
    
    # Configuration de l'image
    cg.add(var.set_file_path(config[CONF_FILE_PATH]))
    cg.add(var.set_output_format_string(config[CONF_OUTPUT_FORMAT]))
    cg.add(var.set_byte_order_string(config[CONF_BYTE_ORDER]))
    
    # Définir le type pour la compatibilité
    cg.add(var.set_image_type(config[CONF_TYPE]))
    
    if CONF_RESIZE in config:
        cg.add(var.set_resize(config[CONF_RESIZE][0], config[CONF_RESIZE][1]))
    
    # Configuration des propriétés image standard
    if image.CONF_TRANSPARENCY in config:
        cg.add(var.set_transparency(config[image.CONF_TRANSPARENCY]))
    
    return var

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
    if hasattr(image, 'IMAGE_TYPE'):
        image.IMAGE_TYPE["SD_IMAGE"] = SdImageEncoder
    
    # Ajouter aussi aux formats d'image reconnus pour la compatibilité LVGL
    if hasattr(image, 'IMAGE_TYPE_SCHEMA'):
        # Étendre le schéma des types d'images
        image.IMAGE_TYPE_SCHEMA = cv.one_of(
            *list(image.IMAGE_TYPE_SCHEMA.validators), 
            "SD_IMAGE", 
            upper=True
        )

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
        
        cg.add(var.set_output_format_string(config.get(CONF_OUTPUT_FORMAT, "RGB565")))
        cg.add(var.set_byte_order_string(config.get(CONF_BYTE_ORDER, "LITTLE_ENDIAN")))
        
        return var
    
    return None

# Configuration d'initialisation
def setup_hooks():
    """Configure les hooks d'intégration avec ESPHome"""
    # Enregistrer le type d'image
    register_sd_image_type()
    
    # Ajouter le hook pour les images si disponible
    if hasattr(image, 'register_image_hook'):
        image.register_image_hook(image_to_code_hook)

# Appeler setup lors de l'import
try:
    setup_hooks()
except Exception as e:
    _LOGGER.warning(f"Failed to setup hooks: {e}")
