import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PLATFORM, CONF_WIDTH, CONF_HEIGHT, CONF_FORMAT
from esphome import automation
from esphome.components import display

DEPENDENCIES = ['sd_mmc_card', 'display']
CODEOWNERS = ["@youkorr"]

# Configuration constants
CONF_STORAGE = "storage"
CONF_PATH = "path"
CONF_ROOT_PATH = "root_path"  # Added missing constant

# Constants pour SD direct
CONF_SD_COMPONENT = "sd_component"

# Constants pour les images SD
CONF_SD_IMAGES = "sd_images"
CONF_FILE_PATH = "file_path"
CONF_BYTE_ORDER = "byte_order"  # Added missing constant

storage_ns = cg.esphome_ns.namespace('storage')
StorageComponent = storage_ns.class_('StorageComponent', cg.Component)
# Fix: Use display.DisplayBuffer.BaseImage or create without inheritance
SdImageComponent = storage_ns.class_('SdImageComponent', cg.Component)

SdImageLoadAction = storage_ns.class_('SdImageLoadAction', automation.Action)
SdImageUnloadAction = storage_ns.class_('SdImageUnloadAction', automation.Action)

# Formats d'image supportés (JPEG/PNG uniquement)
IMAGE_FORMAT = {
    "rgb565": "RGB565",
    "rgb888": "RGB888", 
    "rgba": "RGBA",
}

# Byte order options
BYTE_ORDER = {
    "little_endian": "LITTLE_ENDIAN",
    "big_endian": "BIG_ENDIAN",
}

SD_IMAGE_SCHEMA = cv.Schema({
    cv.Required(CONF_ID): cv.declare_id(SdImageComponent),
    cv.Required(CONF_FILE_PATH): cv.string,
    # Pour JPEG/PNG, width/height sont optionnels (autodétection)
    cv.Optional(CONF_WIDTH, default=0): cv.positive_int,
    cv.Optional(CONF_HEIGHT, default=0): cv.positive_int,
    # Format de sortie souhaité
    cv.Optional(CONF_FORMAT, default="rgb565"): cv.enum(IMAGE_FORMAT, lower=True),
    # Ajouter le type pour la compatibilité LVGL
    cv.Optional("type"): cv.enum(IMAGE_FORMAT, upper=True),
    # Added byte_order support
    cv.Optional(CONF_BYTE_ORDER, default="little_endian"): cv.enum(BYTE_ORDER, lower=True),
}).extend(cv.COMPONENT_SCHEMA)

CONFIG_SCHEMA = cv.Schema({
    cv.Required(CONF_PLATFORM): cv.one_of("sd_direct", lower=True),
    cv.Required(CONF_ID): cv.declare_id(StorageComponent),
    cv.Required(CONF_SD_COMPONENT): cv.use_id(cg.Component),
    cv.Optional(CONF_ROOT_PATH, default="/"): cv.string,  # Added root_path support
    cv.Optional(CONF_SD_IMAGES, default=[]): cv.ensure_list(SD_IMAGE_SCHEMA),
}).extend(cv.COMPONENT_SCHEMA)

def detect_image_type(file_path):
    """Détecter le type d'image basé sur l'extension"""
    file_path_lower = file_path.lower()
    if file_path_lower.endswith(('.jpg', '.jpeg')):
        return 'jpeg'
    elif file_path_lower.endswith('.png'):
        return 'png'
    else:
        raise cv.Invalid(f"Unsupported image format. Only JPEG (.jpg, .jpeg) and PNG (.png) are supported. Got: {file_path}")

def validate_image_config(img_config):
    if not img_config[CONF_FILE_PATH].startswith("/"):
        raise cv.Invalid("Image file path must be absolute (start with '/')")
    
    # Vérifier que c'est un fichier JPEG ou PNG
    try:
        image_type = detect_image_type(img_config[CONF_FILE_PATH])
    except cv.Invalid as e:
        raise e
    
    # Pour JPEG/PNG, les dimensions sont toujours en autodétection (ignorer les valeurs configurées)
    img_config[CONF_WIDTH] = 0  # 0 = autodétection
    img_config[CONF_HEIGHT] = 0  # 0 = autodétection
    
    # Auto-définir le type basé sur le format si pas défini
    if "type" not in img_config:
        img_config["type"] = IMAGE_FORMAT[img_config[CONF_FORMAT]]
    
    return img_config

def validate_storage_config(config):
    # Validate root_path
    if CONF_ROOT_PATH in config:
        root_path = config[CONF_ROOT_PATH]
        if not root_path.startswith("/"):
            raise cv.Invalid("Root path must be absolute (start with '/')")
    
    # Validate images
    if CONF_SD_IMAGES in config:
        for img_config in config[CONF_SD_IMAGES]:
            validate_image_config(img_config)
    
    return config

# Application des validations
CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, validate_storage_config)
SD_IMAGE_SCHEMA = cv.All(SD_IMAGE_SCHEMA, validate_image_config)

async def to_code(config):
    # Ajout des defines pour ESP32-P4
    cg.add_platformio_option("lib_deps", ["https://github.com/espressif/esp-idf.git"])
    
    
    # Création du composant principal
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    # Configuration du composant
    cg.add(var.set_platform(config[CONF_PLATFORM]))

    # Récupération du composant SD
    sd_component = await cg.get_variable(config[CONF_SD_COMPONENT])
    cg.add(var.set_sd_component(sd_component))

    # Configuration du root_path
    if CONF_ROOT_PATH in config:
        cg.add(var.set_root_path(config[CONF_ROOT_PATH]))

    # Traitement des images SD
    if CONF_SD_IMAGES in config and config[CONF_SD_IMAGES]:
        for img_config in config[CONF_SD_IMAGES]:
            await process_sd_image_config(img_config, var)
        
        cg.add_define("USE_SD_IMAGE")

async def process_sd_image_config(img_config, storage_component):
    # Création ET enregistrement du composant image
    img_var = cg.new_Pvariable(img_config[CONF_ID])
    await cg.register_component(img_var, img_config)

    # Configuration de base
    cg.add(img_var.set_storage_component(storage_component))
    cg.add(img_var.set_file_path(img_config[CONF_FILE_PATH]))
    
    # Configuration du format de sortie souhaité
    format_str = IMAGE_FORMAT[img_config[CONF_FORMAT]]
    cg.add(img_var.set_output_format_string(format_str))

    # Configuration de l'ordre des bytes
    if CONF_BYTE_ORDER in img_config:
        byte_order_str = BYTE_ORDER[img_config[CONF_BYTE_ORDER]]
        cg.add(img_var.set_byte_order_string(byte_order_str))

    # Pas de taille attendue pour JPEG/PNG (sera déterminée après décodage)
    # Pas de configuration des dimensions (autodétection)
    
    # Enregistrer comme image pour ESPHome
    cg.add_global(cg.RawStatement(f"// Register {img_config[CONF_ID].id} as image"))
    
    return img_var

@automation.register_action(
    "sd_image.load",
    SdImageLoadAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(SdImageComponent),
        cv.Optional("file_path"): cv.templatable(cv.string),
    })
)
async def sd_image_load_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    if "file_path" in config:
        template_ = await cg.templatable(config["file_path"], args, cg.std_string)
        cg.add(var.set_file_path(template_))
    parent = await cg.get_variable(config[CONF_ID])
    cg.add(var.set_parent(parent))
    return var

@automation.register_action(
    "sd_image.unload",
    SdImageUnloadAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(SdImageComponent),
    })
)
async def sd_image_unload_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    parent = await cg.get_variable(config[CONF_ID])
    cg.add(var.set_parent(parent))
    return var
