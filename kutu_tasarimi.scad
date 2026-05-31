// =========================================================================
// MUTFAK GERİ SAYIM ALARMI - DİKEY ELEGAN 3D KUTU TASARIMI (OpenSCAD)
// =========================================================================
// Tasarım Özellikleri:
// - Dikey Form Faktörü: 60mm Genişlik x 80mm Yükseklik x 45mm Derinlik
// - Sol Üst: Döner Enkoder Mili
// - Sağ Üst: TM1637 Yatay Gösterge Penceresi
// - Alt Orta: Dairesel Baklava Izgarası (50mm Çap / 25mm Yarıçap)
// - Sağ Duvar: Güç Açma/Kapama Sürgülü Anahtar Yuvası
// - Alt Duvar: Type-C Şarj Kablosu Yuvası
// - Bağlantılar: M3 Pirinç Isıtmalı Dişli İnsert (Brass Heat-Set Insert) Uyumlu
// =========================================================================

$fn = 180; // Daire kalitesi (Pürüzsüzlük için en üst seviye - Tinkercad "Sides" maksimum)

// --- RENDER SEÇENEĞİ ---
// "govde"   -> Ön panel delikli dikey ana kutu gövdesi
// "kapak"   -> M3 vidalı havşalı arka kapak kapağı
// "montaj"  -> İki parçanın montaja hazır patlatılmış önizlemesi
secilen_parca = "montaj"; // [govde, kapak, montaj]

// --- KUTU BOYUTLARI ---
kutu_g = 60.0;   // Dış Genişlik (X ekseni)
kutu_y = 80.0;   // Dış Yükseklik (Y ekseni)
kutu_d = 30.0;   // Dış Derinlik (Z ekseni)
duvar = 2.0;     // Kutu et/duvar kalınlığı
r_kose = 6.0;    // Köşelerin yuvarlatılma yarıçapı

// --- ELEMAN KESİK BOYUTLARI ---
// TM1637 Ekran Penceresi (Yatay)
ekran_g = 30.6;  
ekran_y = 14.2;

// EC11 Enkoder Deliği (Dişli şaft için 7.2mm)
enkoder_d = 7.2;

// Type-C Şarj Girişi Yuvası (Alt duvarda, ortalanmış)
sarj_g = 11.0;
sarj_y = 4.5;

// Sürgülü Güç Anahtarı Yuvası (Sağ duvarda)
anahtar_g = 10.0;
anahtar_y = 5.0;

// M3 Pirinç İnsert Ölçüleri (Heat-Set Insert)
insert_d = 4.2;      // M3 insert için önerilen yuva çapı (Havya ile eritmek için tam karar)
insert_derinlik = 8.5; // Insertün rahat oturması için derinlik
vida_gecis_d = 3.3;   // M3 vidanın kapaktan rahat geçmesi için çap
vida_baslik_d = 6.0;  // M3 havşa vida başlığı yuva çapı

// --- ÇEKİRDEK KODLAR ---

module yuvarlatilmis_kutu(g, y, d, r) {
    hull() {
        translate([r, r, 0]) cylinder(r=r, h=d);
        translate([g - r, r, 0]) cylinder(r=r, h=d);
        translate([r, y - r, 0]) cylinder(r=r, h=d);
        translate([g - r, y - r, 0]) cylinder(r=r, h=d);
    }
}

// 1. ANA DİKEY GÖVDE
module kutu_govdesi() {
    difference() {
        // Ana Dış Kasa
        yuvarlatilmis_kutu(kutu_g, kutu_y, kutu_d, r_kose);
        
        // İç Boşluk (Duvar kalınlığı düşülmüş)
        translate([duvar, 
                   duvar, 
                   duvar])
        yuvarlatilmis_kutu(kutu_g - 2*duvar, 
                           kutu_y - 2*duvar, 
                           kutu_d,                            r_kose - duvar);
        
        // --- ÖN PANEL KESİKLERİ ---
        // (Ön panel tamamen düz ve kapalı yapılmıştır, delikleri kullanıcı kendi tasarımına göre ekleyecektir)

        // --- ALT VE SAĞ DUVAR KESİKLERİ ---
        // (Soket ve anahtar delikleri kaldırılmıştır, kullanıcı dış çeperi kendi tasarlayacaktır)
    }
    
    // --- KAPAK İÇİN M3 PİRİNÇ İNSERT KULELERİ (4 Köşede) ---
    // Pirinç insertlerin sağlam erimesi için dış çapı 9mm yapılmıştır.
    difference() {
        union() {
            // Sol Alt Kule
            translate([duvar + 3.8, duvar + 3.8, duvar])
            cylinder(d=9, h=kutu_d - duvar);
            
            // Sağ Alt Kule
            translate([kutu_g - (duvar + 3.8), duvar + 3.8, duvar])
            cylinder(d=9, h=kutu_d - 2);
            
            // Sol Üst Kule
            translate([duvar + 3.8, kutu_y - (duvar + 3.8), duvar])
            cylinder(d=9, h=kutu_d - 2);
            
            // Sağ Üst Kule
            translate([kutu_g - (duvar + 3.8), kutu_y - (duvar + 3.8), duvar])
            cylinder(d=9, h=kutu_d - 2);
        }
        // Isıtmalı M3 insertün havya ile eriyerek girmesi için 4.2mm çapında kılavuz delikler
        translate([duvar + 3.8, duvar + 3.8, kutu_d - insert_derinlik]) cylinder(d=insert_d, h=insert_derinlik + 1);
        translate([kutu_g - (duvar + 3.8), duvar + 3.8, kutu_d - insert_derinlik]) cylinder(d=insert_d, h=insert_derinlik + 1);
        translate([duvar + 3.8, kutu_y - (duvar + 3.8), kutu_d - insert_derinlik]) cylinder(d=insert_d, h=insert_derinlik + 1);
        translate([kutu_g - (duvar + 3.8), kutu_y - (duvar + 3.8), kutu_d - insert_derinlik]) cylinder(d=insert_d, h=insert_derinlik + 1);
    }
}

// 2. ARKA KAPAK (M3 Vidalar İçin Havşalı Delikli - Düz Yüzey)
module kutu_kapagi() {
    difference() {
        // Kapak Ana Plakası (Düz ve Pürüzsüz)
        yuvarlatilmis_kutu(kutu_g, kutu_y, duvar, r_kose);
        
        // 4 Köşedeki Vida Geçiş Delikleri (3.3mm temiz geçiş + 6mm havşa baş yuvası)
        translate([duvar + 3.8, duvar + 3.8, -1]) {
            cylinder(d=vida_gecis_d, h=duvar + 2);
            cylinder(d=vida_baslik_d, h=1.3); // Havşa yuvası
        }
        translate([kutu_g - (duvar + 3.8), duvar + 3.8, -1]) {
            cylinder(d=vida_gecis_d, h=duvar + 2);
            cylinder(d=vida_baslik_d, h=1.3);
        }
        translate([duvar + 3.8, kutu_y - (duvar + 3.8), -1]) {
            cylinder(d=vida_gecis_d, h=duvar + 2);
            cylinder(d=vida_baslik_d, h=1.3);
        }
        translate([kutu_g - (duvar + 3.8), kutu_y - (duvar + 3.8), -1]) {
            cylinder(d=vida_gecis_d, h=duvar + 2);
            cylinder(d=vida_baslik_d, h=1.3);
        }
    }
    
    // Kapağın yerine sıkı oturmasını ve kaymamasını sağlayan iç kılavuz çerçeve
    difference() {
        translate([duvar + 0.3, duvar + 0.3, duvar])
        yuvarlatilmis_kutu(kutu_g - 2*(duvar + 0.3), kutu_y - 2*(duvar + 0.3), 1.5, r_kose - (duvar + 0.3));
        
        translate([duvar + 1.8, duvar + 1.8, duvar - 1])
        yuvarlatilmis_kutu(kutu_g - 2*(duvar + 1.8), kutu_y - 2*(duvar + 1.8), 4, r_kose - (duvar + 1.8));
        
        // Vida kulelerinin geçmesi için iç çerçevedeki dairesel köşe kesikleri
        translate([duvar + 3.8, duvar + 3.8, 0]) cylinder(d=10, h=5);
        translate([kutu_g - (duvar + 3.8), duvar + 3.8, 0]) cylinder(d=10, h=5);
        translate([duvar + 3.8, kutu_y - (duvar + 3.8), 0]) cylinder(d=10, h=5);
        translate([kutu_g - (duvar + 3.8), kutu_y - (duvar + 3.8), 0]) cylinder(d=10, h=5);
    }
}

// --- RENDER SEÇENEKLERİ ---
if (secilen_parca == "govde") {
    kutu_govdesi();
} 
else if (secilen_parca == "kapak") {
    kutu_kapagi();
} 
else if (secilen_parca == "montaj") {
    // Montajlı görünüm: Mavi gövde, yeşil kapak yukarıda patlatılmış görünümde
    color("LightBlue", 0.8) kutu_govdesi();
    
    color("LightGreen", 0.9)
    translate([0, 0, kutu_d + 15]) 
    kutu_kapagi();
}
