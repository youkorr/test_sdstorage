```
# Exemple de configuration YAML pour le composant storage

# Configuration de la carte SD (doit exister d'abord)
sd_mmc_card:
  id: sd_card
  clk_pin: GPIO4
  cmd_pin: GPIO5
  d0_pin: GPIO6
  # Autres pins selon votre configuration...

# Configuration du composant storage
storage:
  id: storage_component
  platform: sd_direct
  sd_component: sd_card
  root_path: "/" 
  sd_images:
    - id: test_jpeg
      file_path: "/images/test.jpg"
      format: rgb565
      byte_order: little_endian
      
    - id: test_png
      file_path: "/images/logo.png"
      format: rgb565
      byte_order: little_endian
      
    - id: raw_image
      file_path: "/images/bitmap.raw"
      width: 320
      height: 240
      format: rgb565
      byte_order: little_endian

# Configuration display (exemple)
display:
  - platform: ili9xxx  # ou votre type d'écran
    id: my_display
    # Configuration de votre écran...
    lambda: |-
      // Utilisation des images SD dans votre display lambda
      if (id(test_jpeg).is_loaded()) {
        id(test_jpeg).draw(0, 0, it, COLOR_ON, COLOR_OFF);
      }

# Automatisation pour charger/décharger les images
on_boot:
  then:
    # Charger une image au démarrage
    - sd_image.load:
        id: test_jpeg
        file_path: "/images/startup.jpg"

# Action pour changer d'image
button:
  - platform: template
    name: "Load Different Image"
    on_press:
      - sd_image.unload:
          id: test_jpeg
      - sd_image.load:
          id: test_jpeg
          file_path: "/images/different.jpg"

```
