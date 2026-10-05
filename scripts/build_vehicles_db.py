import os
import re
import json

# =============================================================================
# CONFIGURATION (Paths are relative to the project root)
# =============================================================================
SOURCE_DIR = r'data/AMS2_CDFs'
OUTPUT_FILE = r'data/vehicles_db.json'
# =============================================================================

def parse_cdf(file_path):
    """
    Parse un fichier CDF pour extraire les paramètres, leurs réglages 
    et leurs plages (ranges) de manière structurée par section.
    """
    car_data = {}
    current_section = "GENERAL" # Section par défaut si rien n'est spécifié

    # Regex pour capturer : ParamNameSetting=X ou ParamNameRange=(min, step, max)
    setting_re = re.compile(r'^(\w+)Setting\s*=\s*([\d\.-]+)', re.IGNORECASE)
    range_re = re.compile(r'^(\w+)Range\s*=\s*\(([\d\.-]+),\s*([\d\.-]+),\s*([\d\.-]+)\)')

    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('//'): 
                    continue

                # Détection de section [SECTION]
                section_match = re.match(r'^\[(\w+)\]', line)
                if section_match:
                    current_section = section_match.group(1)
                    if current_section not in car_data:
                        car_data[current_section] = {}
                    continue

                # Initialisation de la section si elle n'existe pas encore
                if current_section not in car_data:
                    car_data[current_section] = {}

                # Extraction du Setting (Valeur par défaut/actuelle)
                setting_match = setting_re.match(line)
                if setting_match:
                    param, val = setting_match.groups()
                    if param not in car_data[current_section]:
                        car_data[current_section][param] = {}
                    car_data[current_section][param]['default'] = float(val)

                # Extraction du Range (Contraintes)
                range_match = range_re.match(line)
                if range_match:
                    param, r_min, r_step, r_max = range_match.groups()
                    if param not in car_data[current_section]:
                        car_data[current_section][param] = {}
                    car_data[current_section][param]['min'] = float(r_min)
                    car_data[current_section][param]['step'] = float(r_step)
                    car_data[current_section][param]['max'] = float(r_max)

        return car_data
    except Exception as e:
        print(f"  [!] Erreur lors de la lecture de {file_path}: {e}")
        return None

def build_database():
    """Scanne le dossier source et compile tout dans un fichier JSON unique."""
    master_db = {}

    # On définit la racine du projet (le parent du dossier 'scripts')
    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    abs_source_dir = os.path.normpath(os.path.join(project_root, SOURCE_DIR))
    abs_output_file = os.path.normpath(os.path.join(project_root, OUTPUT_FILE))

    # Création du dossier source s'il n'existe pas pour éviter les erreurs au premier lancement
    if not os.path.exists(abs_source_dir):
        print(f"[*] Dossier de source introuvable : {abs_source_dir}")
        print(f"[*] Veuillez placer vos fichiers .cdf dans ce dossier.")
        return

    files = [f for f in os.listdir(abs_source_dir) if f.endswith('.cdf')]
    
    if not files:
        print(f"[*] Aucun fichier .cdf trouvé dans {abs_source_dir}.")
        print(f"[*] Veuillez copier vos fichiers CDF dans ce dossier pour générer la base de données.")
        return

    print(f"[*] Début du scan de {len(files)} fichiers...")

    for filename in files:
        car_id = filename.replace('.cdf', '')
        file_path = os.path.join(abs_source_dir, filename)
        
        print(f"  > Traitement de : {car_id}")
        params = parse_cdf(file_path)
        
        if params:
            master_db[car_id] = {
                "metadata": {
                    "source_file": filename,
                    "total_sections": len(params)
                },
                "parameters": params
            }

    # Sauvegarde du résultat
    output_dir = os.path.dirname(abs_output_file)
    if output_dir and not os.path.exists(output_dir):
        os.makedirs(output_dir)

    with open(abs_output_file, 'w', encoding='utf-8') as f:
        json.dump(master_db, f, indent=4)
    
    print("\n" + "="*40)
    print("COMPILATION TERMINÉE")
    print(f"Fichier généré : {abs_output_file}")
    print(f"Véhicules intégrés : {len(master_db)}")
    print("="*40)

if __name__ == "__main__":
    build_database()
